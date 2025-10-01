#pragma once

#include <sys/arena_alloc.h>
#define N_BUCKETS 512

typedef zio_cksum_t zc_eck;
typedef zio_cksum_t cksum_seqno;
typedef uint64_t txg_birth;
typedef long long unsigned int llu_t;

typedef struct C_map_node {
  struct C_map_node *next;
  cksum_seqno key;
  zc_eck value;
  txg_birth txg_id;
} C_map_node_t;

typedef struct C_map {
  C_map_node_t
      *hash_map[N_BUCKETS]; // N-bucket linked-list for fine distribution
} C_map_t;

// function declarations
__attribute__((unused)) extern void init(C_map_t *map);
__attribute__((unused)) extern void cleanup_global_variable(C_map_t *map);
__attribute__((unused)) extern C_map_node_t **
get_bucket(C_map_t *map, const cksum_seqno *key, const zc_eck *value);
__attribute__((unused)) extern void append_hash(C_map_t *map,
                                                const cksum_seqno *key,
                                                const zc_eck *value,
                                                const txg_birth txg);
__attribute__((unused)) extern zc_eck get_hash(C_map_t *map,
                                               const cksum_seqno *key);
__attribute__((unused)) extern void print(C_map_t *map);
__attribute__((unused)) extern void release_hash(void *prev_hash);
__attribute__((unused)) extern void *
get_serialized_hash(C_map_t *map, const cksum_seqno *key);
__attribute__((unused)) extern int record_exists(const C_map_node_t *node,
                                                 const cksum_seqno *key);
