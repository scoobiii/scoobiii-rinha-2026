#pragma once
#include <stdint.h>
#include <stddef.h>
#include "vectorizer.h"

#define LABEL_LEGIT 0
#define LABEL_FRAUD 1

/* On-disk IVF format:
 *   Header (32 bytes):
 *     uint32_t magic      = 0x52494E48  ('RINH')
 *     uint32_t n_centroids
 *     uint32_t n_vectors
 *     uint32_t dims       = 14
 *     uint32_t reserved[4]
 *
 *   Centroids: n_centroids * dims * float32
 *
 *   For each centroid c (0..n_centroids-1):
 *     uint32_t list_size
 *     list_size * (dims * int16_t + 1 byte label)
 *       → vectors quantized to int16 in range [-32767, 32767]
 *         (-1 sentinel maps to -32767)
 */

#define IVF_MAGIC 0x52494E48u

typedef struct {
    float    centroid[DIMS];
    uint32_t offset;   /* byte offset into list_data */
    uint32_t size;     /* number of vectors in this list */
} ivf_cell_t;

typedef struct {
    /* quantized vector: scale factor 32767 for [0,1] dims;
       -32767 used as sentinel for -1 dims */
    int16_t  v[DIMS];
    uint8_t  label;   /* LABEL_LEGIT or LABEL_FRAUD */
} __attribute__((packed)) ivf_entry_t;

typedef struct {
    uint32_t    n_centroids;
    uint32_t    n_vectors;
    ivf_cell_t *cells;      /* [n_centroids] */
    ivf_entry_t *entries;   /* [n_vectors] — mmap'd or malloc'd */
    int          mmap_fd;
    void        *mmap_ptr;
    size_t       mmap_size;
} ivf_index_t;

typedef struct {
    float    dist;
    uint8_t  label;
} knn_result_t;

int  ivf_load(ivf_index_t *idx, const char *path);
void ivf_free(ivf_index_t *idx);
int  ivf_search(const ivf_index_t *idx, const float *query,
                int k, knn_result_t *out);
