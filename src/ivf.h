#ifndef IVF_H
#define IVF_H

#include <stdint.h>
#include <stddef.h>
#include "vectorizer.h"

#define IVF_MAGIC 0x53434249 // "SCBI"
#define LABEL_LEGIT 0
#define LABEL_FRAUD 1

typedef struct {
    vec_t v[DIMS];
    uint8_t label;
} __attribute__((packed)) ivf_entry_t;

typedef struct {
    float centroid[DIMS];
    uint32_t size;
    uint32_t offset;
} ivf_cell_t;

typedef struct {
    uint32_t n_centroids;
    uint32_t n_vectors;
    ivf_cell_t *cells;
    ivf_entry_t *entries;
    int mmap_fd;
    void *mmap_ptr;
    size_t mmap_size;
} ivf_index_t;

typedef struct {
    float dist;
    uint8_t label;
} knn_result_t;

int ivf_load(ivf_index_t *idx, const char *path);
void ivf_free(ivf_index_t *idx);
int ivf_search(const ivf_index_t *idx, const vec_t *query, int k, knn_result_t *out);

#endif
