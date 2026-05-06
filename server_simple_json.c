#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include "cJSON.h"

#define PORT 9999
#define N_DIMS 14
#define N_NEIGHBORS 5
#define THRESHOLD 0.6f
#define MAX_VECTORS 1000000
#define BUFFER_SIZE 65536

typedef struct {
    int8_t* vectors;
    uint8_t* labels;
    int n_vectors;
    int dims;
} VectorIndex;

VectorIndex* g_idx = NULL;

// Constantes
const float MAX_AMOUNT = 10000.0f;
const float MAX_INSTALLMENTS = 12.0f;
const float AMOUNT_VS_AVG_RATIO = 10.0f;
const float MAX_KM = 1000.0f;
const float MAX_TX_COUNT_24H = 20.0f;
const float MAX_MERCHANT_AVG = 10000.0f;

float clamp(float value, float max_val) {
    float r = value / max_val;
    if (r < 0) return 0;
    if (r > 1) return 1;
    return r;
}

void quantize_int8(float* in, int8_t* out, int dims) {
    for (int i = 0; i < dims; i++) {
        int v = (int)(in[i] * 127.0f);
        if (v < -127) v = -127;
        if (v > 127) v = 127;
        out[i] = (int8_t)v;
    }
}

float euclidean_distance_int8(int8_t* a, int8_t* b, int dims) {
    int sum = 0;
    for (int i = 0; i < dims; i++) {
        int diff = a[i] - b[i];
        sum += diff * diff;
    }
    return sqrtf((float)sum);
}

int hour_from_timestamp(const char* ts) {
    int h;
    sscanf(ts, "%*d-%*d-%*dT%d:%*d:%*d", &h);
    return h;
}

void vectorize(cJSON* payload, float* out) {
    cJSON* tx = cJSON_GetObjectItem(payload, "transaction");
    cJSON* customer = cJSON_GetObjectItem(payload, "customer");
    cJSON* merchant = cJSON_GetObjectItem(payload, "merchant");
    cJSON* terminal = cJSON_GetObjectItem(payload, "terminal");
    cJSON* last = cJSON_GetObjectItem(payload, "last_transaction");
    
    float amount = cJSON_GetObjectItem(tx, "amount")->valuedouble;
    out[0] = clamp(amount, MAX_AMOUNT);
    
    int installments = cJSON_GetObjectItem(tx, "installments")->valueint;
    out[1] = clamp((float)installments, 12.0f);
    
    float avg_amount = cJSON_GetObjectItem(customer, "avg_amount")->valuedouble;
    float amount_vs_avg = (amount / avg_amount) / 10.0f;
    out[2] = amount_vs_avg > 1.0f ? 1.0f : amount_vs_avg;
    
    const char* requested_at = cJSON_GetObjectItem(tx, "requested_at")->valuestring;
    int hour = hour_from_timestamp(requested_at);
    out[3] = (float)hour / 23.0f;
    out[4] = 0.5f;
    
    if (last && cJSON_IsObject(last)) {
        float last_km = cJSON_GetObjectItem(last, "km_from_current")->valuedouble;
        out[5] = 0.5f;
        out[6] = clamp(last_km, MAX_KM);
    } else {
        out[5] = -1.0f;
        out[6] = -1.0f;
    }
    
    float km_home = cJSON_GetObjectItem(terminal, "km_from_home")->valuedouble;
    out[7] = clamp(km_home, MAX_KM);
    
    int tx_count = cJSON_GetObjectItem(customer, "tx_count_24h")->valueint;
    out[8] = clamp((float)tx_count, MAX_TX_COUNT_24H);
    
    int is_online = cJSON_GetObjectItem(terminal, "is_online")->valueint;
    out[9] = is_online ? 1.0f : 0.0f;
    
    int card_present = cJSON_GetObjectItem(terminal, "card_present")->valueint;
    out[10] = card_present ? 1.0f : 0.0f;
    
    const char* merchant_id = cJSON_GetObjectItem(merchant, "id")->valuestring;
    cJSON* known = cJSON_GetObjectItem(customer, "known_merchants");
    int unknown = 1;
    if (known && cJSON_IsArray(known)) {
        for (int i = 0; i < cJSON_GetArraySize(known); i++) {
            cJSON* km = cJSON_GetArrayItem(known, i);
            if (strcmp(km->valuestring, merchant_id) == 0) {
                unknown = 0;
                break;
            }
        }
    }
    out[11] = (float)unknown;
    out[12] = 0.5f;
    
    float merchant_avg = cJSON_GetObjectItem(merchant, "avg_amount")->valuedouble;
    out[13] = clamp(merchant_avg, MAX_MERCHANT_AVG);
}

void knn_search(int8_t* query, int k, int* indices, float* distances) {
    for (int i = 0; i < k; i++) {
        distances[i] = 1e10f;
        indices[i] = -1;
    }
    
    for (int i = 0; i < g_idx->n_vectors; i++) {
        float dist = euclidean_distance_int8(query, &g_idx->vectors[i * N_DIMS], N_DIMS);
        if (dist < distances[k-1]) {
            int pos = k-1;
            while (pos > 0 && dist < distances[pos-1]) {
                distances[pos] = distances[pos-1];
                indices[pos] = indices[pos-1];
                pos--;
            }
            distances[pos] = dist;
            indices[pos] = i;
        }
    }
}

int load_json_dataset(const char* path) {
    printf("📦 Carregando dataset de %s...\n", path);
    
    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("❌ Erro ao abrir %s\n", path);
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* content = (char*)malloc(fsize + 1);
    fread(content, 1, fsize, fp);
    content[fsize] = 0;
    fclose(fp);
    
    cJSON* json = cJSON_Parse(content);
    free(content);
    
    if (!json || !cJSON_IsArray(json)) {
        printf("❌ Erro ao parsear JSON\n");
        return -1;
    }
    
    g_idx = (VectorIndex*)calloc(1, sizeof(VectorIndex));
    g_idx->dims = N_DIMS;
    g_idx->vectors = (int8_t*)malloc(MAX_VECTORS * N_DIMS * sizeof(int8_t));
    g_idx->labels = (uint8_t*)malloc(MAX_VECTORS * sizeof(uint8_t));
    
    int array_size = cJSON_GetArraySize(json);
    printf("  Array tem %d elementos\n", array_size);
    
    int fraud_count = 0;
    for (int i = 0; i < array_size && i < MAX_VECTORS; i++) {
        cJSON* item = cJSON_GetArrayItem(json, i);
        cJSON* vec_array = cJSON_GetObjectItem(item, "vector");
        cJSON* label = cJSON_GetObjectItem(item, "label");
        
        if (vec_array && cJSON_IsArray(vec_array)) {
            float temp[N_DIMS];
            int vec_size = cJSON_GetArraySize(vec_array);
            for (int d = 0; d < N_DIMS && d < vec_size; d++) {
                temp[d] = cJSON_GetArrayItem(vec_array, d)->valuedouble;
            }
            quantize_int8(temp, &g_idx->vectors[g_idx->n_vectors * N_DIMS], N_DIMS);
            g_idx->labels[g_idx->n_vectors] = (label && strcmp(label->valuestring, "fraud") == 0) ? 1 : 0;
            if (g_idx->labels[g_idx->n_vectors] == 1) fraud_count++;
            g_idx->n_vectors++;
        }
    }
    
    cJSON_Delete(json);
    printf("✅ Dataset: %d vetores, %d fraudes\n", g_idx->n_vectors, fraud_count);
    return 0;
}

void handle_request(int client_fd, char* body) {
    cJSON* json = cJSON_Parse(body);
    if (!json) {
        const char* error = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        write(client_fd, error, strlen(error));
        return;
    }
    
    float vec_f32[N_DIMS];
    vectorize(json, vec_f32);
    
    int8_t query[N_DIMS];
    quantize_int8(vec_f32, query, N_DIMS);
    
    int indices[N_NEIGHBORS];
    float distances[N_NEIGHBORS];
    knn_search(query, N_NEIGHBORS, indices, distances);
    
    int fraud_count = 0;
    for (int i = 0; i < N_NEIGHBORS; i++) {
        if (indices[i] >= 0 && g_idx->labels[indices[i]] == 1) {
            fraud_count++;
        }
    }
    
    float score = (float)fraud_count / N_NEIGHBORS;
    bool approved = score < THRESHOLD;
    
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"approved\":%s,\"fraud_score\":%.2f}\r\n",
        approved ? "true" : "false", score);
    
    write(client_fd, response, strlen(response));
    cJSON_Delete(json);
}

void* handle_connection(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (n > 0) {
        buffer[n] = '\0';
        char* body = strstr(buffer, "\r\n\r\n");
        if (body) {
            body += 4;
            handle_request(client_fd, body);
        } else {
            const char* health = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, health, strlen(health));
        }
    }
    
    close(client_fd);
    return NULL;
}

int main() {
    printf("🚀 Rinha C - Dataset JSON\n");
    printf("=======================\n");
    
    if (load_json_dataset("../example-references.json") != 0) {
        printf("❌ Erro ao carregar dataset\n");
        return 1;
    }
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 100) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("✅ API rodando na porta %d\n", PORT);
    printf("📊 %d vetores carregados\n\n", g_idx->n_vectors);
    
    while (1) {
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd >= 0) {
            pthread_t thread;
            pthread_create(&thread, NULL, handle_connection, client_fd);
            pthread_detach(thread);
        } else {
            free(client_fd);
        }
    }
    
    return 0;
}
