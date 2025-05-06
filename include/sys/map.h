#pragma once
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #ifndef USERSPACE
// #define USERSPACE
// #endif

#include <sys/arena_alloc.h>
#define N_BUCKETS 5

#if 0
typedef struct zio_cksum {
  uint64_t zc_word[4];
} zio_cksum_t;
#endif

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
__attribute__((unused)) extern C_map_node_t **get_bucket(C_map_t *map, const cksum_seqno *key, const zc_eck *value);
__attribute__((unused)) extern void append_hash(C_map_t *map, const cksum_seqno *key, const zc_eck *value,
                 const txg_birth txg);
__attribute__((unused)) extern zc_eck get_hash(C_map_t *map, const cksum_seqno *key);
__attribute__((unused)) extern void print(C_map_t *map);
__attribute__((unused)) extern void release_hash(void* prev_hash);
__attribute__((unused)) extern void* get_serialized_hash(C_map_t *map, const cksum_seqno* key);
__attribute__((unused)) extern int record_exists(const C_map_node_t* node, const cksum_seqno* key);


#if 0
// function definitions
__attribute__((unused)) static void cleanup_global_variable(C_map_t *map) {
  // perform cleanup tasks here
  for (int i = 0; i < N_BUCKETS; i++) {
    if (map->hash_map[i] != NULL) {
      C_map_node_t *tmp = map->hash_map[i];
      while (tmp != NULL) {
        C_map_node_t *next = tmp->next;
        free_node(tmp, sizeof(C_map_node_t));
        tmp = next;
      }
    }
  }
}

__attribute__((unused)) static C_map_node_t **get_bucket(C_map_t *map, const cksum_seqno *key,
                          const zc_eck *value) {
  for (int i = 0; i < N_BUCKETS; i++) {
    if (map->hash_map[i] != NULL) {
      if (memcmp(map->hash_map[i]->key.zc_word, key->zc_word,
                 sizeof(cksum_seqno) - sizeof(uint64_t)) == 0) {
        
        if (value == NULL)
          return &map->hash_map[i];
        
        if (memcmp(map->hash_map[i]->key.zc_word, key->zc_word,
                   sizeof(cksum_seqno)) == 0) {
          zfs_dbgmsg(" the record already exists\n");
          if (memcmp(map->hash_map[i]->value.zc_word, value->zc_word, sizeof(zc_eck)) != 0) {
            zfs_dbgmsg(" error in %s, dublicate different hashes\n", __func__);
          }
          return NULL;
        }
        return &map->hash_map[i];
      }
    } else {
      return &map->hash_map[i];
    }
  }
  // should never reach at this point anyways
  return NULL;
}

__attribute__((unused)) static int record_exists(const C_map_node_t* node, const cksum_seqno* key) {
  // returns 0 if record exists;
  return memcmp(node->key.zc_word, key->zc_word, sizeof(cksum_seqno));
}

__attribute__((unused)) static void append_hash(C_map_t *map, const cksum_seqno *key, const zc_eck *value,
                 const txg_birth txg) {
  // zfs_dbgmsg(" %s\n", __func__);

  C_map_node_t **head = get_bucket(map, key, value);
  if (!head) {
    return;
  }
  if (*head) {
    C_map_node_t *tmp = *head;
    while (tmp) {
      if (tmp->next) {
        if (record_exists(tmp->next, key) != 0)
          tmp = tmp->next;
        else 
          return;
      }
      else {
        tmp->next = (C_map_node_t *)alloc_node(sizeof(C_map_node_t));
        tmp->next->txg_id = txg;
        tmp->next->key = *key;
        tmp->next->value = *value;
        tmp->next->next = NULL; // initialize the next pointer
        return;
      }
    }
  } else {
    *head = (C_map_node_t *)alloc_node(sizeof(C_map_node_t));
    (*head)->txg_id = txg;
    (*head)->key = *key;
    (*head)->value = *value;
    (*head)->next = NULL; // initialize the next pointer
  }
  return;
}

__attribute__((unused)) static zc_eck get_hash(C_map_t *map, const cksum_seqno *key) {
  // zfs_dbgmsg(" %s\n", __func__);
  C_map_node_t **bucket = get_bucket(map, key, NULL);
  if (bucket && *bucket) {
    C_map_node_t *tmp = *bucket;
    while (tmp) {
      if (memcmp(tmp->key.zc_word, key->zc_word, sizeof(cksum_seqno)) == 0)
        return tmp->value;
      else {
        tmp = tmp->next;
      }
    }
  }
  zc_eck empty_value = {0}; // return an empty value if not found
  return empty_value;
}

__attribute__((unused)) static void* get_serialized_hash(C_map_t *map, const cksum_seqno* key) {
  zc_eck value = get_hash(map, key);
  void* ret_val = (void*) alloc_node(sizeof(zc_eck));
  memcpy(ret_val, value.zc_word, sizeof(zc_eck));
  return ret_val;
}

__attribute__((unused)) static void release_hash(void* prev_hash) {
  free_node(prev_hash, sizeof(zc_eck));
}

#if 0
C_map_node_t* get_hash(C_map_t *map, const cksum_seqno *key) {
  C_map_node_t **bucket = get_bucket(map, key);
  if (bucket && *bucket) {
    C_map_node_t *tmp = *bucket;
    while (tmp) {
      if (memcmp(tmp->key.zc_word, key->zc_word, sizeof(cksum_seqno)) == 0)
        return tmp;
      else {
        tmp = tmp->next;
      }
    }
  }
  return NULL;
}
#endif

__attribute__((unused)) static void print(C_map_t *map) {
  _printf("**************************\n");
  for (int i = 0; i < N_BUCKETS; i++) {
    zfs_dbgmsg("hash_map[%d]:\n", i);
    C_map_node_t *tmp = map->hash_map[i];
    while (tmp != NULL) {
      zc_eck value = get_hash(map, &(tmp->key));
      // C_map_node_t* node = get_node(map, &(tmp->key));

      zfs_dbgmsg("key=%016llx:%016llx:%016llx:%016llx\t "
                 "value=%016llx:%016llx:%016llx:%016llx\t txg=%llu\n",
                 (llu_t)tmp->key.zc_word[0], (llu_t)tmp->key.zc_word[1],
                 (llu_t)tmp->key.zc_word[2], (llu_t)tmp->key.zc_word[3],
                 (llu_t)value.zc_word[0], (llu_t)value.zc_word[1],
                 (llu_t)value.zc_word[2], (llu_t)value.zc_word[3],
                 (llu_t)tmp->txg_id);
      tmp = tmp->next;
    }
  }
  _printf("**************************\n");
}

#endif