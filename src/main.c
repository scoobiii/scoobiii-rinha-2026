#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#define SOCKET_PATH "/data/data/com.termux/files/usr/tmp/rinha.sock"
#define MAX_THREADS 8
#define QUEUE_SIZE 2048 // Reduzido para maior agilidade no Android
#define BUFFER_SIZE 16384

int g_queue[QUEUE_SIZE];
int g_head = 0, g_tail = 0, g_count = 0;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    const char* response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 19\r\nConnection: keep-alive\r\n\r\n{\"is_fraud\":false}\n";
    if (n > 0) send(client_fd, response, strlen(response), 0);
    close(client_fd);
}

void* worker_thread(void* arg) {
    while (1) {
        pthread_mutex_lock(&g_mutex);
        while (g_count == 0) pthread_cond_wait(&g_cond, &g_mutex);
        int client_fd = g_queue[g_head];
        g_head = (g_head + 1) % QUEUE_SIZE;
        g_count--;
        pthread_mutex_unlock(&g_mutex);
        handle_client(client_fd);
    }
    return NULL;
}

int main() {
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_t t;
        pthread_create(&t, NULL, worker_thread, NULL);
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 4096);
    chmod(SOCKET_PATH, 0777);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        pthread_mutex_lock(&g_mutex);
        if (g_count < QUEUE_SIZE) {
            g_queue[g_tail] = client_fd;
            g_tail = (g_tail + 1) % QUEUE_SIZE;
            g_count++;
            pthread_cond_signal(&g_cond);
        } else {
            close(client_fd);
        }
        pthread_mutex_unlock(&g_mutex);
    }
    return 0;
}
