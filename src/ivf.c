#include "ivf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <float.h>
#include <arm_neon.h>

// Distância Euclidiana Quadrada com ARM NEON
static inline float dist2_neon(const vec_t *q, const vec_t *v) {
    int16x8_t v_q = vld1q_s16(q);
    int16x8_t v_v = vld1q_s16(v);
    int16x8_t diff = vsubq_s16(v_q, v_v);
    int32x4_t sq = vmull_s16(vget_low_s16(diff), vget_low_s16(diff));
    sq = vmlal_s16(sq, vget_high_s16(diff), vget_high_s16(diff));
    int32_t sum = vaddvq_s32(sq);
    for(int i = 8; i < DIMS; i++) {
        int32_t d = (int32_t)q[i] - (int32_t)v[i];
        sum += d * d;
    }
    return (float)sum;
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

int ivf_search(const ivf_index_t *idx, const vec_t *query, int k, knn_result_t *out) {
    if (!idx->n_centroids || !idx->n_vectors) return 0;

    // ALVO ATUALIZADO: nprobe=3 para bater p99 < 2ms
    int nprobe = (idx->n_centroids < 3) ? idx->n_centroids : 3;

    struct { float d; uint32_t c; } cands[2048];
    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        cands[c].d = centroid_dist2(query, idx->cells[c].centroid);
        cands[c].c = c;
    }

    for (int i = 0; i < nprobe; i++) {
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
            float d = dist2_neon(query, list[i].v);
            if (found < k) {
                out[found].dist = d; out[found].label = list[i].label;
                found++;
                if (d > worst_dist || found == 1) worst_dist = d;
            } else if (d < worst_dist) {
                int worst_idx = 0;
                for (int j = 1; j < k; j++) if (out[j].dist > out[worst_idx].dist) worst_idx = j;
                out[worst_idx].dist = d; out[worst_idx].label = list[i].label;
                worst_dist = out[0].dist;
                for (int j = 1; j < k; j++) if (out[j].dist > worst_dist) worst_dist = out[j].dist;
            }
        }
    }
    return found;
}
