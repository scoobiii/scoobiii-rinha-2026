#include "ivf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <float.h>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define HAS_NEON 1
#endif

/* Distância Euclidiana Quadrada com SIMD (NEON/fallback) */
static inline float dist2_simd(const vec_t *q, const vec_t *v) {
#ifdef HAS_NEON
    int16x8_t v_q = vld1q_s16(q);
    int16x8_t v_v = vld1q_s16(v);
    int16x8_t diff = vsubq_s16(v_q, v_v);
    int32x4_t sq = vmull_s16(vget_low_s16(diff), vget_low_s16(diff));
    sq = vmlal_s16(sq, vget_high_s16(diff), vget_high_s16(diff));
    int32_t sum = vaddvq_s32(sq);
    for (int i = 8; i < DIMS; i++) {
        int32_t d = (int32_t)q[i] - (int32_t)v[i];
        sum += d * d;
    }
    return (float)sum;
#else
    int32_t sum = 0;
    for (int i = 0; i < DIMS; i++) {
        int32_t d = (int32_t)q[i] - (int32_t)v[i];
        sum += d * d;
    }
    return (float)sum;
#endif
}

static inline float centroid_dist2(const vec_t *q, const float *c) {
    float d = 0.0f;
    for (int i = 0; i < DIMS; i++) {
        float fq = (float)q[i] / 32767.0f;
        float diff = fq - c[i];
        d += diff * diff;
    }
    return d;
}

int ivf_load(ivf_index_t *idx, const char *path) {
    memset(idx, 0, sizeof(*idx));
    idx->mmap_fd = -1;

    FILE *f = fopen(path, "rb");
    if (!f) { perror("ivf_load: fopen"); return -1; }

    uint32_t hdr[8];
    if (fread(hdr, sizeof(uint32_t), 8, f) != 8) {
        fprintf(stderr, "IVF: header corrompido\n");
        fclose(f);
        return -1;
    }
    if (hdr[0] != IVF_MAGIC) {
        fprintf(stderr, "IVF: magic mismatch (esperado %08x, lido %08x)\n", IVF_MAGIC, hdr[0]);
        fclose(f);
        return -1;
    }

    idx->n_centroids = hdr[1];
    idx->n_vectors   = hdr[2];

    idx->cells = malloc(idx->n_centroids * sizeof(ivf_cell_t));
    if (!idx->cells) { fclose(f); return -1; }

    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        if (fread(idx->cells[c].centroid, sizeof(float), DIMS, f) != DIMS) {
            fprintf(stderr, "IVF: erro ao ler centroides\n");
            free(idx->cells);
            fclose(f);
            return -1;
        }
    }

    uint32_t *sizes = malloc(idx->n_centroids * sizeof(uint32_t));
    if (fread(sizes, sizeof(uint32_t), idx->n_centroids, f) != idx->n_centroids) {
        fprintf(stderr, "IVF: erro ao ler list_sizes\n");
        free(sizes);
        free(idx->cells);
        fclose(f);
        return -1;
    }

    uint32_t offset = 0;
    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        idx->cells[c].size = sizes[c];
        idx->cells[c].offset = offset;
        offset += sizes[c];
    }
    free(sizes);

    long data_pos = ftell(f);
    fclose(f);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("ivf_load: open"); free(idx->cells); return -1; }

    struct stat st;
    fstat(fd, &st);
    idx->mmap_size = (size_t)st.st_size;
    idx->mmap_ptr = mmap(NULL, idx->mmap_size, PROT_READ, MAP_SHARED, fd, 0);
    if (idx->mmap_ptr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        free(idx->cells);
        return -1;
    }
    idx->mmap_fd = fd;
    idx->entries = (ivf_entry_t *)((char *)idx->mmap_ptr + data_pos);

    return 0;
}

void ivf_free(ivf_index_t *idx) {
    if (idx->mmap_ptr && idx->mmap_ptr != MAP_FAILED)
        munmap(idx->mmap_ptr, idx->mmap_size);
    if (idx->mmap_fd >= 0) close(idx->mmap_fd);
    if (idx->cells) free(idx->cells);
    memset(idx, 0, sizeof(*idx));
}

int ivf_search(const ivf_index_t *idx, const vec_t *query, int k, knn_result_t *out) {
    if (!idx->n_centroids || !idx->n_vectors) return 0;

    // nprobe = 3 (otimizado para baixa latência)
    int nprobe = (idx->n_centroids < 3) ? idx->n_centroids : 3;

    struct { float d; uint32_t c; } cands[2048];
    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        cands[c].d = centroid_dist2(query, idx->cells[c].centroid);
        cands[c].c = c;
    }

    for (int i = 0; i < nprobe && i < (int)idx->n_centroids; i++) {
        for (uint32_t j = i + 1; j < idx->n_centroids; j++) {
            if (cands[j].d < cands[i].d) {
                float td = cands[i].d; uint32_t tc = cands[i].c;
                cands[i].d = cands[j].d; cands[i].c = cands[j].c;
                cands[j].d = td; cands[j].c = tc;
            }
        }
    }

    int found = 0;
    float worst_dist = FLT_MAX;
    for (int ci = 0; ci < nprobe; ci++) {
        uint32_t cell = cands[ci].c;
        uint32_t sz = idx->cells[cell].size;
        const ivf_entry_t *list = idx->entries + idx->cells[cell].offset;

        for (uint32_t i = 0; i < sz; i++) {
            float d = dist2_simd(query, list[i].v);
            if (found < k) {
                out[found].dist = d;
                out[found].label = list[i].label;
                found++;
                if (d > worst_dist || found == 1) worst_dist = d;
            } else if (d < worst_dist) {
                int worst_idx = 0;
                for (int j = 1; j < k; j++)
                    if (out[j].dist > out[worst_idx].dist) worst_idx = j;
                out[worst_idx].dist = d;
                out[worst_idx].label = list[i].label;
                worst_dist = out[0].dist;
                for (int j = 1; j < k; j++)
                    if (out[j].dist > worst_dist) worst_dist = out[j].dist;
            }
        }
    }
    return found;
}
