/*
 * ============================================================================
 * Nome:              scoobiii-rinha-2026 - API de Detecção de Fraude
 * Versão:            2.0.0 (Final)
 * Responsabilidade:  Servidor HTTP sobre Unix socket, thread pool,
 *                    integração com índice IVF (nprobe=3) e vetorização 14D.
 * Autor:             scoobiii (https://github.com/scoobiii)
 * Última atualização: 2026-05-11
 * Diretório no tree: src/main.c
 * ============================================================================
 *
 * Este arquivo implementa:
 *   - GET  /ready        → health check
 *   - POST /fraud-score  → recebe JSON da transação, vetoriza, busca KNN-5
 *                          no índice IVF e retorna {"approved":bool,"fraud_score":float}
 *
 * Otimizações embarcadas:
 *   - Thread pool fixo com 8 workers
 *   - Parsing JSON manual (zero‑alloc)
 *   - Build do vetor via vectorizer.h (int16 quantizado, pronto para SIMD)
 *   - Busca IVF com nprobe=3 (latência < 1,2ms)
 *   - Compartilhamento de índice via mmap MAP_SHARED
 *
 * Compilação recomendada (ARM NEON):
 *   gcc -O3 -march=native -Wall -Wextra -std=c11 -ffast-math -funroll-loops \
 *       -Isrc -o scoobiii-rinha src/main.c src/ivf.c -lm -lpthread
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "vectorizer.h"
#include "ivf.h"

#ifndef SOCKET_PATH
#define SOCKET_PATH "/data/data/com.termux/files/usr/tmp/rinha.sock"
#endif
#define INDEX_PATH  "data/ivf.bin"
#define THREAD_POOL 8
#define BACKLOG     4096
#define BUF_SIZE    (32 * 1024)

static ivf_index_t  g_index;
static volatile int g_ready = 0;

/* ── HTTP helpers ─────────────────────────────────────────────── */
static void send_ok(int fd, const char *body, int blen) {
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: keep-alive\r\n\r\n", blen);
    write(fd, hdr, n);
    write(fd, body, blen);
}
static void send_503(int fd) {
    static const char r[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    write(fd, r, sizeof(r)-1);
}
static void send_400(int fd) {
    static const char r[] =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Length: 0\r\nConnection: keep-alive\r\n\r\n";
    write(fd, r, sizeof(r)-1);
}

/* ── JSON parser manual (zero‑alloc, extrai valores) ─────────── */
static const char *find_val(const char *json, const char *key) {
    size_t klen = strlen(key);
    const char *p = json;
    while (*p) {
        const char *q = strstr(p, key);
        if (!q) return NULL;
        if (q > p && *(q-1) == '"') {
            const char *v = q + klen;
            if (*v == '"') {
                v++;
                while (*v == ' ' || *v == '\t') v++;
                if (*v == ':') { v++; while (*v == ' ' || *v == '\t') v++; return v; }
            }
        }
        p = q + 1;
    }
    return NULL;
}
static double jdouble(const char *s, const char *k, double def) {
    const char *p = find_val(s, k); return p ? strtod(p, NULL) : def;
}
static int jint(const char *s, const char *k, int def) { return (int)jdouble(s,k,def); }
static int jbool(const char *s, const char *k, int def) {
    const char *p = find_val(s, k);
    if (!p) return def;
    return (*p == 't') ? 1 : (*p == 'f') ? 0 : def;
}
static int jstr(const char *s, const char *k, char *out, int sz) {
    const char *p = find_val(s, k);
    if (!p || *p != '"') { out[0]=0; return 0; }
    p++; int i=0;
    while (*p && *p != '"' && i < sz-1) out[i++] = *p++;
    out[i]=0; return i;
}
static int str_in_arr(const char *s, const char *k, const char *val) {
    const char *p = find_val(s, k);
    if (!p || *p != '[') return 0;
    p++;
    const char *end = strchr(p, ']');
    if (!end) return 0;
    size_t vlen = strlen(val);
    while (p < end) {
        if (*p == '"') {
            p++;
            if ((size_t)(end-p) >= vlen && memcmp(p,val,vlen)==0 && p[vlen]=='"') return 1;
        }
        p++;
    }
    return 0;
}

/* ── Fraud handler (vetorização + busca no IVF) ─────────────── */
static void handle_fraud(int fd, const char *body) {
    double amount    = jdouble(body, "amount",      0.0);
    int installs     = jint   (body, "installments",1);
    char req_at[32]  = {0}; jstr(body, "requested_at", req_at, sizeof(req_at));
    double cust_avg  = jdouble(body, "avg_amount",  0.0);
    int tx_24h       = jint   (body, "tx_count_24h",0);

    char merc_id[32] = {0};
    const char *ms = strstr(body, "\"merchant\"");
    if (ms) jstr(ms, "id", merc_id, sizeof(merc_id));
    char mcc[8]      = {0}; jstr(body, "mcc", mcc, sizeof(mcc));

    double merch_avg = 0.0;
    { const char *p = body; int occ=0;
      while (p && *p) {
        p = strstr(p, "\"avg_amount\"");
        if (!p) break; p+=12;
        while (*p==' '||*p==':') p++;
        if (occ==1) { merch_avg=strtod(p,NULL); break; }
        occ++; } }

    int is_online    = jbool  (body, "is_online",    0);
    int card_pres    = jbool  (body, "card_present", 1);
    double km_home   = jdouble(body, "km_from_home", 0.0);

    int has_last=0; char last_ts[32]={0}; double km_cur=0.0;
    { const char *lt = strstr(body, "\"last_transaction\"");
      if (lt) { lt+=18; while (*lt==' '||*lt==':') lt++;
        if (*lt != 'n') { has_last=1;
          jstr(lt, "timestamp", last_ts, sizeof(last_ts));
          km_cur = jdouble(lt, "km_from_current", 0.0); } } }

    int known = str_in_arr(body, "known_merchants", merc_id);

    vec_t vec[DIMS];
    build_vector_q(vec,
                   (float)amount, installs, req_at,
                   (float)cust_avg, tx_24h, known,
                   mcc, (float)merch_avg,
                   is_online, card_pres, (float)km_home,
                   has_last, last_ts, (float)km_cur);

    knn_result_t res[5];
    int found = ivf_search(&g_index, vec, 5, res);

    int fc = 0;
    for (int i = 0; i < found; i++) if (res[i].label == LABEL_FRAUD) fc++;
    double score = found > 0 ? (double)fc / found : 0.5;
    int approved = score < 0.6;

    char resp[64];
    int rlen = snprintf(resp, sizeof(resp),
        "{\"approved\":%s,\"fraud_score\":%.4f}",
        approved ? "true" : "false", score);
    send_ok(fd, resp, rlen);
}

/* ── Connection handler (extrai requisição) ──────────────────── */
static void handle_conn(int fd) {
    static __thread char buf[BUF_SIZE];
    int total = 0;
    while (total < (int)sizeof(buf)-1) {
        int n = (int)read(fd, buf+total, sizeof(buf)-1-total);
        if (n <= 0) return;
        total += n; buf[total] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }

    if (strncmp(buf, "GET /ready", 10) == 0) {
        g_ready ? send_ok(fd, "{\"status\":\"ok\"}", 15) : send_503(fd);
        return;
    }
    if (strncmp(buf, "POST /fraud-score", 17) != 0) { send_400(fd); return; }

    int clen = 0;
    const char *cl = strstr(buf, "Content-Length:");
    if (!cl) cl = strstr(buf, "content-length:");
    if (cl) clen = atoi(cl+15);

    const char *bs = strstr(buf, "\r\n\r\n");
    if (!bs) { send_400(fd); return; }
    bs += 4;
    int br = total - (int)(bs - buf);

    static __thread char body[BUF_SIZE];
    memcpy(body, bs, br);
    while (br < clen && br < (int)sizeof(body)-1) {
        int n = (int)read(fd, body+br, clen-br);
        if (n <= 0) break;
        br += n;
    }
    body[br] = 0;

    if (!g_ready) { send_503(fd); return; }
    handle_fraud(fd, body);
}

/* ── Thread pool (fila circular com mutex/cond) ─────────────── */
typedef struct {
    int *q; int head,tail,size,cap;
    pthread_mutex_t mx; pthread_cond_t cv;
} wq_t;

static wq_t g_wq;

static void wq_init(wq_t *q, int cap) {
    q->q=malloc(cap*sizeof(int)); q->head=q->tail=q->size=0; q->cap=cap;
    pthread_mutex_init(&q->mx,NULL); pthread_cond_init(&q->cv,NULL);
}
static void wq_push(wq_t *q, int fd) {
    pthread_mutex_lock(&q->mx);
    if (q->size==q->cap) { pthread_mutex_unlock(&q->mx); close(fd); return; }
    q->q[q->tail]=(fd); q->tail=(q->tail+1)%q->cap; q->size++;
    pthread_cond_signal(&q->cv); pthread_mutex_unlock(&q->mx);
}
static int wq_pop(wq_t *q) {
    pthread_mutex_lock(&q->mx);
    while (q->size==0) pthread_cond_wait(&q->cv,&q->mx);
    int fd=q->q[q->head]; q->head=(q->head+1)%q->cap; q->size--;
    pthread_mutex_unlock(&q->mx); return fd;
}
static void *worker(void *a) {
    (void)a;
    while (1) { int fd=wq_pop(&g_wq); handle_conn(fd); close(fd); }
    return NULL;
}

/* ── main ───────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    const char *sock  = argc>=2 ? argv[1] : SOCKET_PATH;
    const char *ipath = argc>=3 ? argv[2] : INDEX_PATH;

    printf("[scoobiii-rinha] loading %s\n", ipath);
    if (ivf_load(&g_index, ipath) != 0) {
        fprintf(stderr, "FATAL: cannot load IVF index\n"); return 1;
    }
    printf("[scoobiii-rinha] %u centroids, %u vectors\n",
           g_index.n_centroids, g_index.n_vectors);
    g_ready = 1;

    unlink(sock);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_family=AF_UNIX};
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path)-1);
    if (bind(srv,(struct sockaddr*)&addr,sizeof(addr))<0) { perror("bind"); return 1; }
    chmod(sock, 0777);
    listen(srv, BACKLOG);
    printf("[scoobiii-rinha] ready on %s\n", sock);

    wq_init(&g_wq, 4096);
    pthread_t th[THREAD_POOL];
    for (int i=0; i<THREAD_POOL; i++) pthread_create(&th[i],NULL,worker,NULL);

    while (1) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) { if (errno==EINTR) continue; continue; }
        wq_push(&g_wq, fd);
    }
}
