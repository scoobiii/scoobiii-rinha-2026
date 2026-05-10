/*
 * preprocess.c — Build IVF binary index from references.json.gz
 * Usage: ./preprocess /data/references.json.gz /data/ivf.bin [n_centroids]
 *
 * Steps:
 *   1. Decompress references.json.gz (streaming, zlib)
 *   2. Parse JSON → float[14] vectors + label
 *   3. K-means clustering (k=n_centroids)
 *   4. Assign each vector to nearest centroid
 *   5. Write IVF binary: header + centroids + list_sizes + entries
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <zlib.h>

#include "ivf.h"

#define DEFAULT_N_CENTROIDS 1024
#define KMEANS_ITERS        20
#define CHUNK               (1 << 20)  /* 1MB decompress chunks */
#define MAX_VECS            3200000

/* ── Flat vector store ──────────────────────────────────────── */
typedef struct {
    float   v[DIMS];
    uint8_t label;
} flat_vec_t;

static flat_vec_t *g_vecs = NULL;
static uint32_t    g_nvec = 0;

/* ── Streaming JSON buffer ──────────────────────────────────── */
typedef struct {
    char  *buf;
    size_t buf_cap;
    size_t buf_len;
    size_t pos;
} jbuf_t;

static int jbuf_init(jbuf_t *j) {
    j->buf_cap = 1 << 24; /* 16MB */
    j->buf     = malloc(j->buf_cap);
    j->buf_len = 0;
    j->pos     = 0;
    return j->buf ? 0 : -1;
}

static void jbuf_append(jbuf_t *j, const char *data, size_t n) {
    if (j->buf_len + n > j->buf_cap) {
        size_t remaining = j->buf_len - j->pos;
        memmove(j->buf, j->buf + j->pos, remaining);
        j->buf_len = remaining;
        j->pos     = 0;
        if (j->buf_len + n > j->buf_cap) {
            j->buf_cap = (j->buf_len + n) * 2;
            j->buf = realloc(j->buf, j->buf_cap);
        }
    }
    memcpy(j->buf + j->buf_len, data, n);
    j->buf_len += n;
}

static inline void skip_ws(jbuf_t *j) {
    while (j->pos < j->buf_len) {
        char c = j->buf[j->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') j->pos++;
        else break;
    }
}

static int read_float(jbuf_t *j, float *out) {
    skip_ws(j);
    char *end;
    *out = strtof(j->buf + j->pos, &end);
    if (end == j->buf + j->pos) return 0;
    j->pos = (size_t)(end - j->buf);
    return 1;
}

static int read_label(jbuf_t *j, uint8_t *out) {
    skip_ws(j);
    if (j->pos >= j->buf_len) return 0;
    if (j->buf[j->pos] != '"') return 0;
    j->pos++;
    if (j->pos + 5 <= j->buf_len && memcmp(j->buf+j->pos, "fraud", 5) == 0) {
        *out = LABEL_FRAUD; j->pos += 5;
    } else if (j->pos + 5 <= j->buf_len && memcmp(j->buf+j->pos, "legit", 5) == 0) {
        *out = LABEL_LEGIT; j->pos += 5;
    } else {
        return 0;
    }
    if (j->pos < j->buf_len && j->buf[j->pos] == '"') j->pos++;
    return 1;
}

static int expect_char(jbuf_t *j, char c) {
    skip_ws(j);
    if (j->pos >= j->buf_len || j->buf[j->pos] != c) return 0;
    j->pos++;
    return 1;
}

static int skip_to(jbuf_t *j, char c) {
    while (j->pos < j->buf_len && j->buf[j->pos] != c) j->pos++;
    return (j->pos < j->buf_len);
}

static int parse_record(jbuf_t *j, flat_vec_t *rec) {
    skip_ws(j);
    if (j->pos >= j->buf_len) return 0;
    char c = j->buf[j->pos];
    if (c == ']') return -1;
    if (c == ',') { j->pos++; skip_ws(j); }
    if (j->pos >= j->buf_len) return 0;
    c = j->buf[j->pos];
    if (c == ']') return -1;
    if (c != '{') return -1;

    size_t saved = j->pos;
    size_t depth = 0, p = j->pos;
    while (p < j->buf_len) {
        if (j->buf[p] == '{') depth++;
        else if (j->buf[p] == '}') { depth--; if (depth == 0) { p++; break; } }
        p++;
    }
    if (depth != 0) { j->pos = saved; return 0; }

    if (!expect_char(j, '{')) goto fail;

    int got_vec = 0, got_lbl = 0;
    for (int key = 0; key < 2; key++) {
        skip_ws(j);
        if (j->pos < j->buf_len && j->buf[j->pos] == ',') j->pos++;
        if (!expect_char(j, '"')) goto fail;
        if (j->pos + 6 <= j->buf_len &&
            memcmp(j->buf+j->pos, "vector", 6) == 0) {
            j->pos += 6;
            if (!expect_char(j, '"')) goto fail;
            if (!expect_char(j, ':')) goto fail;
            if (!expect_char(j, '[')) goto fail;
            for (int d = 0; d < DIMS; d++) {
                if (!read_float(j, &rec->v[d])) goto fail;
                if (d < DIMS-1 && !expect_char(j, ',')) goto fail;
            }
            if (!expect_char(j, ']')) goto fail;
            got_vec = 1;
        } else if (j->pos + 5 <= j->buf_len &&
                   memcmp(j->buf+j->pos, "label", 5) == 0) {
            j->pos += 5;
            if (!expect_char(j, '"')) goto fail;
            if (!expect_char(j, ':')) goto fail;
            if (!read_label(j, &rec->label)) goto fail;
            got_lbl = 1;
        } else {
            goto fail;
        }
    }
    skip_ws(j);
    if (j->pos < j->buf_len && j->buf[j->pos] == '}') j->pos++;
    return (got_vec && got_lbl) ? 1 : -1;

fail:
    j->pos = saved;
    if (!skip_to(j, '}')) return 0;
    j->pos++;
    return -2;
}

/* ── K-means ────────────────────────────────────────────────── */
static void kmeans(float *centroids, const flat_vec_t *vecs,
                   uint32_t n, uint32_t k, int iters) {
    srand(42);
    for (uint32_t i = 0; i < k; i++) {
        uint32_t chosen = (uint32_t)rand() % n;
        memcpy(centroids + i * DIMS, vecs[chosen].v, DIMS * sizeof(float));
    }

    uint32_t *assign = malloc(n * sizeof(uint32_t));
    double   *sums   = malloc(k * DIMS * sizeof(double));
    uint32_t *counts = malloc(k * sizeof(uint32_t));

    for (int iter = 0; iter < iters; iter++) {
        for (uint32_t i = 0; i < n; i++) {
            float best_d = FLT_MAX; uint32_t best_c = 0;
            for (uint32_t c = 0; c < k; c++) {
                float d = 0.0f;
                const float *cv = centroids + c * DIMS;
                for (int d2 = 0; d2 < DIMS; d2++) {
                    float diff = vecs[i].v[d2] - cv[d2];
                    d += diff * diff;
                }
                if (d < best_d) { best_d = d; best_c = c; }
            }
            assign[i] = best_c;
        }
        memset(sums,   0, k * DIMS * sizeof(double));
        memset(counts, 0, k * sizeof(uint32_t));
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = assign[i]; counts[c]++;
            double *s = sums + c * DIMS;
            for (int d = 0; d < DIMS; d++) s[d] += vecs[i].v[d];
        }
        for (uint32_t c = 0; c < k; c++) {
            if (counts[c] == 0) continue;
            float *cv = centroids + c * DIMS;
            double *s = sums + c * DIMS;
            for (int d = 0; d < DIMS; d++) cv[d] = (float)(s[d] / counts[c]);
        }
        if (iter % 5 == 0)
            fprintf(stderr, "k-means iter %d/%d\n", iter+1, iters);
    }
    free(assign); free(sums); free(counts);
}

/* ── Main ───────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <references.json.gz> <ivf.bin> [n_centroids]\n", argv[0]);
        return 1;
    }
    const char *in_path  = argv[1];
    const char *out_path = argv[2];
    uint32_t    k        = (argc >= 4) ? (uint32_t)atoi(argv[3]) : DEFAULT_N_CENTROIDS;

    g_vecs = malloc(MAX_VECS * sizeof(flat_vec_t));
    if (!g_vecs) { fprintf(stderr, "OOM\n"); return 1; }

    gzFile gz = gzopen(in_path, "rb");
    if (!gz) { fprintf(stderr, "Cannot open %s\n", in_path); return 1; }
    gzbuffer(gz, 1 << 20);

    jbuf_t jb; jbuf_init(&jb);
    char *chunk = malloc(CHUNK);
    int in_array = 0;
    flat_vec_t rec;

    fprintf(stderr, "Parsing %s...\n", in_path);
    while (!gzeof(gz)) {
        int n = gzread(gz, chunk, CHUNK);
        if (n <= 0) break;
        jbuf_append(&jb, chunk, (size_t)n);

        while (1) {
            skip_ws(&jb);
            if (jb.pos >= jb.buf_len) break;
            if (!in_array) {
                if (jb.buf[jb.pos] == '[') { jb.pos++; in_array = 1; }
                else { jb.pos++; }
                continue;
            }
            int r = parse_record(&jb, &rec);
            if (r == 1) {
                if (g_nvec < MAX_VECS) g_vecs[g_nvec++] = rec;
                if (g_nvec % 100000 == 0)
                    fprintf(stderr, "  parsed %u vectors\n", g_nvec);
            } else if (r == 0) {
                break;
            } else if (r == -1) {
                goto done_parsing;
            }
        }
    }
done_parsing:
    gzclose(gz); free(chunk); free(jb.buf);
    fprintf(stderr, "Parsed %u vectors total\n", g_nvec);
    if (g_nvec == 0) { fprintf(stderr, "No vectors!\n"); return 1; }

    if (k > g_nvec) k = g_nvec;
    fprintf(stderr, "Running k-means k=%u iters=%d...\n", k, KMEANS_ITERS);
    float *centroids = malloc(k * DIMS * sizeof(float));
    kmeans(centroids, g_vecs, g_nvec, k, KMEANS_ITERS);

    fprintf(stderr, "Assigning vectors to centroids...\n");
    uint32_t *assign     = malloc(g_nvec * sizeof(uint32_t));
    uint32_t *list_sizes = calloc(k, sizeof(uint32_t));

    for (uint32_t i = 0; i < g_nvec; i++) {
        float best_d = FLT_MAX; uint32_t best_c = 0;
        for (uint32_t c = 0; c < k; c++) {
            float d = 0.0f;
            const float *cv = centroids + c * DIMS;
            for (int d2 = 0; d2 < DIMS; d2++) {
                float diff = g_vecs[i].v[d2] - cv[d2];
                d += diff * diff;
            }
            if (d < best_d) { best_d = d; best_c = c; }
        }
        assign[i] = best_c;
        list_sizes[best_c]++;
    }

    fprintf(stderr, "Writing %s...\n", out_path);
    FILE *out = fopen(out_path, "wb");
    if (!out) { perror(out_path); return 1; }

    uint32_t hdr[8] = { IVF_MAGIC, k, g_nvec, DIMS, 0, 0, 0, 0 };
    fwrite(hdr, sizeof(uint32_t), 8, out);
    for (uint32_t c = 0; c < k; c++)
        fwrite(centroids + c * DIMS, sizeof(float), DIMS, out);
    fwrite(list_sizes, sizeof(uint32_t), k, out);

    uint32_t *offsets = calloc(k, sizeof(uint32_t));
    uint32_t cum = 0;
    for (uint32_t c = 0; c < k; c++) { offsets[c] = cum; cum += list_sizes[c]; }

    ivf_entry_t *sorted = malloc(g_nvec * sizeof(ivf_entry_t));
    uint32_t *cur = calloc(k, sizeof(uint32_t));
    for (uint32_t i = 0; i < g_nvec; i++) {
        uint32_t c = assign[i];
        ivf_entry_t *e = sorted + offsets[c] + cur[c];
        cur[c]++;
        e->label = g_vecs[i].label;
        for (int d = 0; d < DIMS; d++) {
            float vf = g_vecs[i].v[d];
            e->v[d] = (vf == -1.0f) ? -32767 : (int16_t)(vf * 32767.0f);
        }
    }
    fwrite(sorted, sizeof(ivf_entry_t), g_nvec, out);
    fclose(out);

    fprintf(stderr, "Done! Wrote %u vectors into %u cells\n", g_nvec, k);
    free(centroids); free(assign); free(list_sizes);
    free(offsets); free(cur); free(sorted); free(g_vecs);
    return 0;
}
