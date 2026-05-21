#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

#define PORT 9999
#define DIMS 14
#define SCALE_INT16 32767.0f
#define MAX_VECTORS 10000

// Função de distância simplificada (será substituída pelo NEON)
static float vector_distance(const int16_t* a, const int16_t* b) {
    float d = 0.0f;
    for (int i = 0; i < DIMS; i++) {
        float vi = (a[i] == -32767) ? -1.0f : (float)a[i] / SCALE_INT16;
        float vj = (b[i] == -32767) ? -1.0f : (float)b[i] / SCALE_INT16;
        float diff = vi - vj;
        d += diff * diff;
    }
    return d;
}

static void knn_search(const int16_t* query, const int16_t* vectors, int n_vectors,
                       int k, int* results_idx, float* results_dist) {
    for (int i = 0; i < k; i++) {
        results_dist[i] = 1e30f;
        results_idx[i] = -1;
    }
    
    for (int i = 0; i < n_vectors; i++) {
        float dist = vector_distance(query, vectors + i * DIMS);
        
        if (dist < results_dist[k-1]) {
            int pos = k-1;
            while (pos > 0 && dist < results_dist[pos-1]) {
                results_dist[pos] = results_dist[pos-1];
                results_idx[pos] = results_idx[pos-1];
                pos--;
            }
            results_dist[pos] = dist;
            results_idx[pos] = i;
        }
    }
}

static int16_t* vectors;
static int n_vectors = MAX_VECTORS;

void init_data() {
    vectors = malloc(n_vectors * DIMS * sizeof(int16_t));
    printf("📊 Gerando %d vetores sintéticos...\n", n_vectors);
    
    srand(42);
    for (int i = 0; i < n_vectors; i++) {
        for (int j = 0; j < DIMS; j++) {
            vectors[i * DIMS + j] = (int16_t)(rand() % 65535 - 32768);
        }
    }
}

void handle_request(int client_fd) {
    char buffer[4096];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (n <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[n] = '\0';
    
    // GET /ready
    if (strstr(buffer, "GET") && strstr(buffer, "/ready")) {
        const char* resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK\n";
        write(client_fd, resp, strlen(resp));
    }
    // POST /fraud-score
    else if (strstr(buffer, "POST") && strstr(buffer, "/fraud-score")) {
        // Query padrão (usando amount do JSON se disponível)
        int16_t query[DIMS];
        for (int i = 0; i < DIMS; i++) {
            query[i] = 0;
        }
        
        // Tenta extrair amount do body
        char* body = strstr(buffer, "\r\n\r\n");
        if (body) {
            char* amount_ptr = strstr(body, "\"amount\"");
            if (amount_ptr) {
                float amount = 0;
                sscanf(amount_ptr, "\"amount\":%f", &amount);
                query[0] = (int16_t)(fminf(amount / 10000.0f, 1.0f) * SCALE_INT16);
            }
        }
        
        int idx[5];
        float dist[5];
        
        knn_search(query, vectors, n_vectors, 5, idx, dist);
        
        // Calcula fraud_score baseado nas distâncias
        float fraud_score = 0.0f;
        for (int i = 0; i < 5; i++) {
            if (dist[i] < 0.5f) fraud_score += 0.2f;
        }
        if (fraud_score > 1.0f) fraud_score = 1.0f;
        
        int approved = (fraud_score < 0.6f);
        
        char resp[512];
        snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "{\"approved\":%s,\"fraud_score\":%.2f}\n",
            (int)(30 + (approved ? 4 : 5) + (fraud_score < 10 ? 4 : 5)),
            approved ? "true" : "false",
            fraud_score);
        write(client_fd, resp, strlen(resp));
    }
    else {
        const char* resp = "HTTP/1.1 404 Not Found\r\n\r\n";
        write(client_fd, resp, strlen(resp));
    }
    
    close(client_fd);
}

void* worker(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    handle_request(fd);
    return NULL;
}

int main() {
    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║     RINHA 2026 - FRAUD DETECTION API       ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    
    init_data();
    
    printf("\n🔧 Configuração:\n");
    printf("  • Porta: %d\n", PORT);
    printf("  • Vetores: %d\n", n_vectors);
    printf("  • Dimensões: %d\n", DIMS);
    printf("  • K: 5\n");
    
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    
    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server, 128) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("\n✅ Servidor rodando na porta %d\n", PORT);
    printf("   Endpoints:\n");
    printf("     GET  http://localhost:%d/ready\n", PORT);
    printf("     POST http://localhost:%d/fraud-score\n", PORT);
    printf("\n📡 Aguardando requisições...\n\n");
    
    while (1) {
        int* client = malloc(sizeof(int));
        *client = accept(server, NULL, NULL);
        if (*client < 0) {
            free(client);
            continue;
        }
        
        pthread_t t;
        pthread_create(&t, NULL, worker, client);
        pthread_detach(t);
    }
    
    close(server);
    return 0;
}
