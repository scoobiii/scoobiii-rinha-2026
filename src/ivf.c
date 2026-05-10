#include "ivf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <float.h>

/* Euclidean distance squared between float query and int16 stored vector.
   Handles sentinel -1 → stored as -32767. */
static inline float dist2(const float *q, const int16_t *v) {
    float d = 0.0f;
    for (int i = 0; i < DIMS; i++) {
        float vi = (v[i] == -32767) ? -1.0f : (float)v[i] / 32767.0f;
        float diff = q[i] - vi;
        d += diff * diff;
    }
    return d;
}

static inline float centroid_dist2(const float *q, const float *c) {
    float d = 0.0f;
    for (int i = 0; i < DIMS; i++) {
        float diff = q[i] - c[i];
        d += diff * diff;
    }
    return d;
}

int ivf_load(ivf_index_t *idx, const char *path) {
    memset(idx, 0, sizeof(*idx));
    idx->mmap_fd = -1;

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }

    /* Header */
    uint32_t hdr[8];
    if (fread(hdr, sizeof(uint32_t), 8, f) != 8) {
        fprintf(stderr, "IVF: bad header\n"); fclose(f); return -1;
    }
    if (hdr[0] != IVF_MAGIC) {
        fprintf(stderr, "IVF: wrong magic %08x\n", hdr[0]); fclose(f); return -1;
    }
    idx->n_centroids = hdr[1];
    idx->n_vectors   = hdr[2];
    /* hdr[3] = dims, hdr[4..7] reserved */

    /* Centroids */
    idx->cells = malloc(idx->n_centroids * sizeof(ivf_cell_t));
    if (!idx->cells) { fclose(f); return -1; }

    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        if (fread(idx->cells[c].centroid, sizeof(float), DIMS, f) != DIMS) {
            fprintf(stderr, "IVF: centroid read error\n"); fclose(f); return -1;
        }
    }

    /* Compute offsets from list sizes stored inline */
    uint32_t *list_sizes = malloc(idx->n_centroids * sizeof(uint32_t));
    if (!list_sizes) { fclose(f); return -1; }
    if (fread(list_sizes, sizeof(uint32_t), idx->n_centroids, f) != idx->n_centroids) {
        fprintf(stderr, "IVF: list_sizes read error\n"); free(list_sizes); fclose(f); return -1;
    }
    uint32_t offset = 0;
    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        idx->cells[c].size   = list_sizes[c];
        idx->cells[c].offset = offset;
        offset += list_sizes[c];
    }
    free(list_sizes);

    /* mmap entries */
    long pos = ftell(f);
    fclose(f);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    struct stat st;
    fstat(fd, &st);
    size_t sz = (size_t)st.st_size;
    void *ptr = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) { perror("mmap"); close(fd); return -1; }
    idx->mmap_fd   = fd;
    idx->mmap_ptr  = ptr;
    idx->mmap_size = sz;
    idx->entries   = (ivf_entry_t *)((char *)ptr + pos);

    return 0;
}

void ivf_free(ivf_index_t *idx) {
    if (idx->mmap_ptr && idx->mmap_ptr != MAP_FAILED)
        munmap(idx->mmap_ptr, idx->mmap_size);
    if (idx->mmap_fd >= 0) close(idx->mmap_fd);
    free(idx->cells);
    memset(idx, 0, sizeof(*idx));
}

/* Find k nearest neighbors using IVF with nprobe cells */
int ivf_search(const ivf_index_t *idx, const float *query,
               int k, knn_result_t *out) {
    if (!idx->n_centroids || !idx->n_vectors) return 0;

    /* nprobe: search more cells for better recall */
    int nprobe = idx->n_centroids < 32 ? idx->n_centroids : 32;

    /* Find closest nprobe centroids */
    typedef struct { float d; uint32_t c; } cd_t;
    cd_t *cands = malloc(idx->n_centroids * sizeof(cd_t));
    for (uint32_t c = 0; c < idx->n_centroids; c++) {
        cands[c].d = centroid_dist2(query, idx->cells[c].centroid);
        cands[c].c = c;
    }
    /* Partial sort: bubble up nprobe smallest */
    for (int i = 0; i < nprobe && i < (int)idx->n_centroids; i++) {
        for (uint32_t j = i+1; j < idx->n_centroids; j++) {
            if (cands[j].d < cands[i].d) {
                cd_t tmp = cands[i]; cands[i] = cands[j]; cands[j] = tmp;
            }
        }
    }

    /* Max-heap of size k for KNN */
    float  heap_d[5];
    uint8_t heap_l[5];
    int heap_n = 0;

    #define HEAP_PUSH(d, l) do { \
        if (heap_n < k) { \
            /* find max */ \
            heap_d[heap_n] = (d); heap_l[heap_n] = (l); heap_n++; \
        } else { \
            int worst = 0; \
            for (int _i = 1; _i < k; _i++) if (heap_d[_i] > heap_d[worst]) worst = _i; \
            if ((d) < heap_d[worst]) { heap_d[worst] = (d); heap_l[worst] = (l); } \
        } \
    } while(0)

    /* Search each cell */
    for (int ci = 0; ci < nprobe && ci < (int)idx->n_centroids; ci++) {
        uint32_t cell = cands[ci].c;
        uint32_t off  = idx->cells[cell].offset;
        uint32_t sz   = idx->cells[cell].size;
        const ivf_entry_t *list = idx->entries + off;
        for (uint32_t i = 0; i < sz; i++) {
            float d = dist2(query, list[i].v);
            HEAP_PUSH(d, list[i].label);
        }
    }
    free(cands);

    /* Copy results */
    for (int i = 0; i < heap_n; i++) {
        out[i].dist  = heap_d[i];
        out[i].label = heap_l[i];
    }
    return heap_n;
}
