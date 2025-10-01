#include <sys/map.h>

// function definitions

__attribute__((unused)) void init(C_map_t *map) {
  for (int i = 0; i < N_BUCKETS; i++)
    map->hash_map[i] = NULL;
}

__attribute__((unused)) void cleanup_global_variable(C_map_t *map) {
  // perform cleanup tasks here
  int count_of_elems = 0;
  for (int i = 0; i < N_BUCKETS; i++) {
    if (map->hash_map[i] != NULL) {
      C_map_node_t *tmp = map->hash_map[i];
      while (tmp != NULL) {
        C_map_node_t *next = tmp->next;
        free_node(tmp, sizeof(C_map_node_t));
        count_of_elems++;
        tmp = next;
      }
    }
  }
  init(map);
  zfs_dbgmsg(" %s: cleaned up %d elements\n", __func__, count_of_elems);
}

__attribute__((unused)) C_map_node_t **
get_bucket(C_map_t *map, const cksum_seqno *key, const zc_eck *value) {
  int count = 0;
  for (int i = 0; i < N_BUCKETS; i++) {
    if (map->hash_map[i] != NULL) {
      if (memcmp(map->hash_map[i]->key.zc_word, key->zc_word,
                 sizeof(cksum_seqno) - sizeof(uint64_t)) == 0) {

        if (value == NULL)
          return &map->hash_map[i];

        if (memcmp(map->hash_map[i]->key.zc_word, key->zc_word,
                   sizeof(cksum_seqno)) == 0) {
          zfs_dbgmsg(" a record for blk_no=%llu already exists\n",
                     (u_longlong_t)key->zc_word[ZIL_ZC_SEQ]);
          if (memcmp(map->hash_map[i]->value.zc_word, value->zc_word,
                     sizeof(zc_eck)) != 0) {
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
  zfs_dbgmsg(" ** should never reach at this point **\n");
  ASSERT(0);
  return NULL;
}

__attribute__((unused)) int record_exists(const C_map_node_t *node,
                                          const cksum_seqno *key) {
  // returns 0 if record exists;
  return memcmp(node->key.zc_word, key->zc_word, sizeof(cksum_seqno));
}

__attribute__((unused)) void append_hash(C_map_t *map, const cksum_seqno *key,
                                         const zc_eck *value,
                                         const txg_birth txg) {
  // zfs_dbgmsg(" %s\n", __func__);
#if 1
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
      } else {
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
#endif
  return;
}

__attribute__((unused)) zc_eck get_hash(C_map_t *map, const cksum_seqno *key) {
  int count = 0;
  // zfs_dbgmsg(" %s\n", __func__);
  C_map_node_t **bucket = get_bucket(map, key, NULL);
  if (bucket && *bucket) {
    C_map_node_t *tmp = *bucket;
    while (tmp != NULL) {
#if 0
      zfs_dbgmsg(" key=%016llx:%016llx:%016llx:%016llx\n", \
                 (llu_t)tmp->key.zc_word[0], (llu_t)tmp->key.zc_word[1],\
                 (llu_t)tmp->key.zc_word[2], (llu_t)tmp->key.zc_word[3]);
#endif
      if (memcmp(tmp->key.zc_word, key->zc_word, sizeof(cksum_seqno)) == 0) {
        zfs_dbgmsg(" found key after count=%d iterations\n", count);
        return tmp->value;
      } else {
        count++;
        tmp = tmp->next;
      }
    }
  }
  zfs_dbgmsg(" found empty key after count=%d iterations\n", count);

  zc_eck empty_value;
  empty_value.zc_word[0] = 0;
  empty_value.zc_word[1] = 0;
  empty_value.zc_word[2] = 0;
  empty_value.zc_word[3] = 0; // return an empty value if not found
  return empty_value;
}

__attribute__((unused)) void *get_serialized_hash(C_map_t *map,
                                                  const cksum_seqno *key) {
  zc_eck value = get_hash(map, key);
  void *ret_val = (void *)alloc_node(sizeof(zc_eck));
  memcpy(ret_val, value.zc_word, sizeof(zc_eck));
  return ret_val;
}

__attribute__((unused)) void release_hash(void *prev_hash) {
  free_node(prev_hash, sizeof(zc_eck));
}

__attribute__((unused)) void print(C_map_t *map) {
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