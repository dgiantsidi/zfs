#include <sys/commitments.h>
#include <sys/arena_alloc.h>
#include <sys/map.h>
#include <sys/global_map.h>

/*
  typedef struct zil_header {
    uint64_t zh_claim_txg;	// txg in which log blocks were claimed 
    uint64_t zh_replay_seq;	// highest replayed sequence number 
    blkptr_t zh_log;	// log chain 
    uint64_t zh_claim_blk_seq; // highest claimed block sequence number
    uint64_t zh_flags;	// header flags 
    uint64_t zh_claim_lr_seq; // highest claimed lr sequence number
    uint64_t zh_pad[3];
  } zil_header_t;
*/

__attribute__((unused)) void cleanup_ccf_cmts(dyn_array_commitments_t* prev_ccf_cmts) {
  int count = prev_ccf_cmts->count;
  dyn_array_commitments_t* head = prev_ccf_cmts;

  for (int i = 0; i < count; i++) {
    dyn_array_commitments_t* tmp = head;
    head = head->next;
    free_node(tmp->cmt_data, sizeof(zil_commitment_t));
    //if (i > 0)
    free_node(tmp, sizeof(dyn_array_commitments_t));
  }
  // prev_ccf_cmts->count = 0;
}


__attribute__((unused)) void copy_commitments(dyn_array_commitments_t* dst, dyn_array_commitments_t src) {
  zfs_dbgmsg("\n");
  dyn_array_commitments_t* dst_t = dst;
  dyn_array_commitments_t* src_t = &src;

  dst_t->count = src.count;

  int count = src.count;
  zfs_dbgmsg("  src.count=%d dst_t->count=%d\n", count, dst_t->count);

  for (int i = 0; i < count; i++) {
    dst_t->cmt_data = alloc_node(sizeof(zil_commitment_t));
    memcpy(dst_t->cmt_data, src_t->cmt_data, sizeof(zil_commitment_t));
    dst_t->next = alloc_node(sizeof(dyn_array_commitments_t));
    dst_t = dst_t->next;
    src_t = src_t->next;
  }

}

__attribute__((unused)) dyn_array_commitments_t* copy_commitments2(dyn_array_commitments_t* dst, dyn_array_commitments_t src) {
  // zfs_dbgmsg("\n");
  dyn_array_commitments_t* dst_t = dst;
  //dyn_array_commitments_t* dst_t_copy = dst;
  // zfs_dbgmsg("\n");

  dyn_array_commitments_t* src_t = &src;
  dyn_array_commitments_t* ret_dst_t = alloc_node(sizeof(dyn_array_commitments_t));
  dyn_array_commitments_t* ret_dst_t_copy = ret_dst_t;

  // zfs_dbgmsg("\n");
  // zfs_dbgmsg(" ccf_zil_header_commitments=%p\n", (void*)dst_t);

  int total_count = 0;
  int count = src.count;
  // zfs_dbgmsg("  src.count=%d dst_t->count=%d\n", count, dst_t->count);
  // copy the src first which are the most up to date commitments anyway
  for (int i = 0; i < count; i++) {
    ret_dst_t->cmt_data = alloc_node(sizeof(zil_commitment_t));
    memcpy(ret_dst_t->cmt_data, src_t->cmt_data, sizeof(zil_commitment_t));
    ret_dst_t->next = alloc_node(sizeof(dyn_array_commitments_t));
    ret_dst_t = ret_dst_t->next;
    src_t = src_t->next;
    total_count++;
  }
  dyn_array_commitments_t* tail_dst_t = ret_dst_t;
  ret_dst_t = ret_dst_t_copy;
  ret_dst_t->count = total_count;
  ret_dst_t_copy->count = total_count;
  int append_flag = 1; // equals to 1 for append and 0 for update in-place
  int dst_count = dst_t->count;
  int ret_dst_t_count = ret_dst_t->count;
  // append if we need to append non-dirty commitments
  // what about *deleting* the dataset?
  for (int i = 0; i < dst_count; i++) {
    // zfs_dbgmsg(" i=%d, count=%d dst_name=%s\n", i, ret_dst_t->count, dst_t->cmt_data->name);
    for (int j = 0; j < ret_dst_t_count; j++) {
      // int max_size = strlen(ret_dst_t->cmt_data->name) > strlen(dst_t->cmt_data->name) ? strlen(ret_dst_t->cmt_data->name) : strlen(dst_t->cmt_data->name);
      int equal_size = strnlen(ret_dst_t->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) == strnlen(dst_t->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) ? 1 : 0;

      // zfs_dbgmsg("j=%d %s --- %s\n", j, ret_dst_t->cmt_data->name, dst_t->cmt_data->name);
      if (equal_size == 1 && memcmp(ret_dst_t->cmt_data->name, dst_t->cmt_data->name, strnlen(ret_dst_t->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN)) == 0) {
        append_flag = 0;
        // zfs_dbgmsg(" do not append this: %s\n", dst_t->cmt_data->name);
        //break;
      }
      ret_dst_t = ret_dst_t->next;
    }
    if (append_flag) {
      // zfs_dbgmsg(" append this: %s\n", dst_t->cmt_data->name);
      tail_dst_t->cmt_data = alloc_node(sizeof(zil_commitment_t));
      memcpy(tail_dst_t->cmt_data, dst_t->cmt_data, sizeof(zil_commitment_t));
      tail_dst_t->next = alloc_node(sizeof(dyn_array_commitments_t));
      tail_dst_t = tail_dst_t->next;
      total_count++;
    }
    append_flag = 1;
    dst_t = dst_t->next;
    ret_dst_t = ret_dst_t_copy;    
  }
  ret_dst_t = ret_dst_t_copy;
  ret_dst_t->count = total_count;
  

  cleanup_ccf_cmts(dst);
  dst = ret_dst_t;
  zfs_dbgmsg(" total count=%d\n", (dst)->count);
  return dst;

}

__attribute__((unused)) void ccf_commit_cmts(dyn_array_commitments_t* zils_header_commitments, enum commitments_type is_tail) {
  // zfs_dbgmsg("\n");
  if (is_tail == ZIL_TAIL_COMMITMENT) {
    zfs_dbgmsg(" ccf_zil_tail_commitments=%p\n", (void*)zils_header_commitments);
  }
  else {
    zfs_dbgmsg(" ccf_zil_header_commitments=%p\n", (void*)zils_header_commitments);
  }
  if (zils_header_commitments == NULL) {
    zfs_dbgmsg(" NULL \n");
    return;
  }
  
  if (zils_header_commitments->count == 0) {
    zfs_dbgmsg(" emtpy cmts at commit (error!) ..\n");
    return;
  }
  

  dyn_array_commitments_t* head = zils_header_commitments;
  int count = head->count;
  zfs_dbgmsg(" CCF commit %d ZIL commitments ..\n", count);
  for (int i = 0; i < count; i++) {
    dyn_array_commitments_t* tmp = head;
    dump_zil_commitment_from_global_state(tmp);
    head = head->next;
  }
}

__attribute__((unused)) void cleanup_cmts(dyn_array_commitments_t* zils_header_commitments) {
  if (zils_header_commitments->count == 0) {
    zfs_dbgmsg(" emtpy cmts at the cleanup ..\n");
    return;
  }
  

  dyn_array_commitments_t* head = zils_header_commitments;
  int count = head->count;
  zfs_dbgmsg(" need to cleanup %d ZIL commitments ..\n", count);
  for (int i = 0; i < count; i++) {
    dyn_array_commitments_t* tmp = head;
    dump_zil_commitment_from_global_state(tmp);
    head = head->next;
    free_node(tmp->cmt_data, sizeof(zil_commitment_t));
    if (i > 0)
      free_node(tmp, sizeof(dyn_array_commitments_t));

  }

  zils_header_commitments->count = 0;
	zils_header_commitments->cmt_data = NULL; 
	zils_header_commitments->next = NULL;
  
}

__attribute__((unused)) void append_cmts(dyn_array_commitments_t* zils_header_commitments, zil_commitment_t* zil_header_cmt) {
  if (zils_header_commitments->count == 0) {
    dyn_array_commitments_t* head = zils_header_commitments;
    head->count++;
    head->cmt_data = zil_header_cmt;
    //zfs_dbgmsg(" append zil of objset name:%s\n", zil_header_cmt->name);
    head->next = alloc_node(sizeof(dyn_array_commitments_t));
    //zfs_dbgmsg(" ************** current count of commitments=%d **************\n", head->count);
    return;
  }

  //zfs_dbgmsg(" 2 \n");
  dyn_array_commitments_t* head = zils_header_commitments;
  int count = head->count;
  //zfs_dbgmsg(" ************** current count of commitments=%d **************\n", head->count);

  int append_flag = 1; // equals to 1 for append and 0 for update in-place
  for (int i = 0; i < (count-1); i++) {
    //zfs_dbgmsg(" 3 count=%d \n", count);
    // int max_size = strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) > strnlen(zil_header_cmt->name, ZFS_MAX_DATASET_NAME_LEN) ? strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) : strnlen(zil_header_cmt->name, ZFS_MAX_DATASET_NAME_LEN);
    int equal_size = strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) == strnlen(zil_header_cmt->name, ZFS_MAX_DATASET_NAME_LEN) ? 1 : 0;

    //zfs_dbgmsg(" %s\t%s\n", head->cmt_data->name, zil_header_cmt->name);
    if (equal_size == 1 && memcmp(head->cmt_data->name, zil_header_cmt->name, strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN)) == 0) {
      // need to update
      //zfs_dbgmsg(" update zil of objset name:%s\n", head->cmt_data->name);
      append_flag = 0;
      //todo: free the previous cmt_data?
      head->cmt_data = zil_header_cmt;
    }
    head = head->next;
  }
  //zfs_dbgmsg(" 4 \n");
  int equal_size = strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) == strnlen(zil_header_cmt->name, ZFS_MAX_DATASET_NAME_LEN) ? 1 : 0;
  if (equal_size == 1 && memcmp(head->cmt_data->name, zil_header_cmt->name, strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN)) == 0) {
    // need to update
    //zfs_dbgmsg(" update zil of objset name:%s\n", head->cmt_data->name);
    append_flag = 0;
    head->cmt_data = zil_header_cmt;
  }
  //zfs_dbgmsg(" 5 \n");
  if (append_flag) {
    //zfs_dbgmsg(" append zil of objset name:%s\n", zil_header_cmt->name);
    head->next->cmt_data = zil_header_cmt;
    zils_header_commitments->count++;
    head->next->next = alloc_node(sizeof(dyn_array_commitments_t));
  }
  //zfs_dbgmsg(" 6 \n");
  //zfs_dbgmsg(" ************** end count=%d **************\n", zils_header_commitments->count);
  return;
}

__attribute__((unused))  zil_commitment_t* generate_zil_tail_cmt(const char* name, uint64_t txg, const zio_cksum_t blk_cksum, dva_t* allocated_bp) {
  zil_commitment_t* gen_commitment = alloc_node(sizeof(zil_commitment_t));
  gen_commitment->txg_sync = txg;
  gen_commitment->blk_num = blk_cksum;
  memcpy(gen_commitment->name, name, strnlen(name, ZFS_MAX_DATASET_NAME_LEN));
  zio_cksum_t* blk_zc_eck = get_serialized_hash(&cksum_map, &(blk_cksum));

  gen_commitment->blk_digest.zc_word[0] = 0;
  gen_commitment->blk_digest.zc_word[1] = 0;
  gen_commitment->blk_digest.zc_word[2] = 0;
  gen_commitment->blk_digest.zc_word[3] = 0;
  gen_commitment->allocated_bp = *allocated_bp;
  memcpy(gen_commitment->blk_digest.zc_word, blk_zc_eck->zc_word, sizeof(gen_commitment->blk_digest));
  release_hash(blk_zc_eck);
  return gen_commitment;

}


__attribute__((unused)) zil_commitment_t* generate_zil_header_cmt(const zil_header_t* zh, const char* name, uint64_t txg) {
  size_t zil_header_sz = sizeof(zil_header_t);
  void* zh_buf = alloc_node(zil_header_sz);
  memcpy(zh_buf, zh, sizeof(zil_header_t));   // todo: calculate digest

  zil_commitment_t* gen_commitment = alloc_node(sizeof(zil_commitment_t));
  
  gen_commitment->txg_sync = txg;
  gen_commitment->blk_num = zh->zh_log.blk_cksum;
  memcpy(gen_commitment->name, name, strlen(name));
  gen_commitment->allocated_bp = *(zh->zh_log.blk_dva);
  gen_commitment->blk_digest.zc_word[0] = 0;
  gen_commitment->blk_digest.zc_word[1] = 0;
  gen_commitment->blk_digest.zc_word[2] = 0;
  gen_commitment->blk_digest.zc_word[3] = 0;
  free_node(zh_buf, sizeof(zil_header_t));
  return gen_commitment;
}

__attribute__((unused)) void dump_zil_commitment_from_global_state(const dyn_array_commitments_t* zil_cmt) {
  zfs_dbgmsg(" [COMMITMENT-print: name=%s, txg_sync=%llu, blk_num=%016llx:%016llx:%016llx:%016llx, \
    DVA=<%llu:%llx:%llx>, digest=%016llx:%016llx:%016llx:%016llx]", \
    zil_cmt->cmt_data->name, (u_longlong_t)zil_cmt->cmt_data->txg_sync, (u_longlong_t)zil_cmt->cmt_data->blk_num.zc_word[0], \
    (u_longlong_t)zil_cmt->cmt_data->blk_num.zc_word[1], (u_longlong_t)zil_cmt->cmt_data->blk_num.zc_word[2], \
    (u_longlong_t)zil_cmt->cmt_data->blk_num.zc_word[ZIL_ZC_SEQ], (u_longlong_t)DVA_GET_VDEV(&(zil_cmt->cmt_data->allocated_bp)), \
    (u_longlong_t)DVA_GET_OFFSET(&(zil_cmt->cmt_data->allocated_bp)), (u_longlong_t)DVA_GET_ASIZE(&(zil_cmt->cmt_data->allocated_bp)), \
    (u_longlong_t)zil_cmt->cmt_data->blk_digest.zc_word[0], (u_longlong_t)zil_cmt->cmt_data->blk_digest.zc_word[1], \
    (u_longlong_t)zil_cmt->cmt_data->blk_digest.zc_word[2], (u_longlong_t)zil_cmt->cmt_data->blk_digest.zc_word[3]);
}


__attribute__((unused)) zil_commitment_t* dump_zil_commitment(const zil_header_t* zh, const char* name, uint64_t txg) {
  
  zfs_dbgmsg(" [COMMITMENT: name=%s\tzil_header] cksum_seq_no=%016llx:%016llx:%016llx:%016llx txg=%llu DVA=<%llu:%llx:%llx>\n", name, (u_longlong_t)zh->zh_log.blk_cksum.zc_word[0], (u_longlong_t)zh->zh_log.blk_cksum.zc_word[1], (u_longlong_t)zh->zh_log.blk_cksum.zc_word[2], (u_longlong_t)zh->zh_log.blk_cksum.zc_word[ZIL_ZC_SEQ],  (u_longlong_t)txg, (u_longlong_t)DVA_GET_VDEV(zh->zh_log.blk_dva),  (u_longlong_t)DVA_GET_OFFSET(zh->zh_log.blk_dva), (u_longlong_t)DVA_GET_ASIZE(zh->zh_log.blk_dva));
  zil_commitment_t* cmt = generate_zil_header_cmt(zh, name, txg);
  return cmt;
}


__attribute__((unused)) void dump_zil_commitment2(const zil_commitment_t* cmt) {
  zfs_dbgmsg(" [COMMITMENT: name=%s\t] cksum_seq_no=%016llx:%016llx:%016llx:%016llx txg=%llu DVA=<%llu:%llx:%llx>\n", cmt->name, (u_longlong_t)cmt->blk_num.zc_word[0], \
  (u_longlong_t)cmt->blk_num.zc_word[1], (u_longlong_t)cmt->blk_num.zc_word[2], (u_longlong_t)cmt->blk_num.zc_word[ZIL_ZC_SEQ],  (u_longlong_t)cmt->txg_sync, \
  (u_longlong_t)DVA_GET_VDEV(&cmt->allocated_bp),  (u_longlong_t)DVA_GET_OFFSET(&cmt->allocated_bp), (u_longlong_t)DVA_GET_ASIZE(&cmt->allocated_bp));
}

__attribute__((unused)) zil_commitment_t*  get_zil_header_cmt_for_dsl(const char* name, dyn_array_commitments_t* ccf_zil_header_commitments) {
  // todo: return null if ccf_zil_header_commitments is NULL
  if (ccf_zil_header_commitments == NULL) {
    zfs_dbgmsg(" ccf_zil_header_commitments is NULL!\n");
    return NULL;
  }
  #if 1
  dyn_array_commitments_t* head = ccf_zil_header_commitments;
  int count = head->count;

  for (int i = 0; i < count; i++) {
    int equal_size = strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN) == strnlen(name, ZFS_MAX_DATASET_NAME_LEN) ? 1 : 0;
    // zfs_dbgmsg(" %s\t%s\n", head->cmt_data->name, zil_header_cmt->name);
    if (equal_size == 1 && memcmp(head->cmt_data->name, name, strnlen(head->cmt_data->name, ZFS_MAX_DATASET_NAME_LEN)) == 0) {
      // need to update
      zfs_dbgmsg(" found:%s (name=%s)\n", head->cmt_data->name, name);
      return head->cmt_data;
    }
    head = head->next;
  }
  return NULL;
  #else
    zil_commitment_t* cmt = list_head(&(ccf_zil_commitments->zil_blk_commitments));
    return cmt;
  #endif
}


#if 1
__attribute__((unused)) zil_commitment_t* get_zil_tail_cmt_for_dsl(const char* name, dyn_array_commitments_t* ccf_zil_tail_commitments) {
    return get_zil_header_cmt_for_dsl(name, ccf_zil_tail_commitments);
}
#else
__attribute__((unused)) zil_commitment_t* get_zil_tail_cmt_for_dsl( ccf_state_t* ccf_zil_commitments) { //(const char* name, dyn_array_commitments_t* ccf_zil_tail_commitments) {
   
    zil_commitment_t* cmt = list_tail(&(ccf_zil_commitments->zil_blk_commitments));
    return cmt;
}
#endif


#if 1
__attribute__((unused)) void ccf_zil_commitments_protocol(dyn_array_commitments_t* zils_header_commitments, \
	dyn_array_commitments_t* zils_tail_commitments, zil_commitment_t* zil_tail_cmt) {
  // zfs_dbgmsg(" 1\n");
  zil_commitment_t* cmt = get_zil_header_cmt_for_dsl(zil_tail_cmt->name, zils_header_commitments);
  // zfs_dbgmsg(" 2\n");
  if (cmt == NULL) {
    zfs_dbgmsg(" zil_header_commitments is empty!\n");
    return;
  }
  

  zc_eck empty_value;
  empty_value.zc_word[0] = 0;
  empty_value.zc_word[1] = 0;
  empty_value.zc_word[2] = 0;
  empty_value.zc_word[3] = 0; // return an empty value if not found

  if (memcmp(cmt->blk_num.zc_word, zil_tail_cmt->blk_num.zc_word, sizeof(zil_tail_cmt->blk_num)) == 0) {
    //zfs_dbgmsg(" need to update the zil header digest too ..\n");
    if (memcmp(cmt->blk_digest.zc_word, empty_value.zc_word, sizeof(empty_value)) == 0) {
      append_cmts(zils_header_commitments, zil_tail_cmt);
    }
    else {
      zfs_dbgmsg(" update did not happen!\n");
    }
  }
  zil_commitment_t* zil_tail_cmt_copy = alloc_node(sizeof(zil_commitment_t));
  zil_tail_cmt_copy->blk_num = zil_tail_cmt->blk_num;
  zil_tail_cmt_copy->blk_digest = zil_tail_cmt->blk_digest;
  zil_tail_cmt_copy->txg_sync = zil_tail_cmt->txg_sync;
  zil_tail_cmt_copy->allocated_bp = zil_tail_cmt->allocated_bp;
  memcpy(zil_tail_cmt_copy->name, zil_tail_cmt->name, strnlen(zil_tail_cmt->name, ZFS_MAX_DATASET_NAME_LEN));

  append_cmts(zils_tail_commitments, zil_tail_cmt_copy);
}
#endif

#if 0
// 2nd idea
__attribute__((unused)) void ccf_state_init(ccf_state_t* ccf_zil_commitments) {
  zfs_dbgmsg("\n");
  list_create(&(ccf_zil_commitments->zil_blk_commitments), sizeof (zil_commitment_t), 0);
  return;
}

__attribute__((unused)) void ccf_state_append(ccf_state_t* ccf_zil_commitments, zil_commitment_t* zil_cmt) {
  list_insert_tail(&(ccf_zil_commitments->zil_blk_commitments), zil_cmt);
  return;
}

__attribute__((unused)) void ccf_state_cleanup(ccf_state_t* ccf_zil_commitments) {
  (void) ccf_zil_commitments;
  int count = 0; 
  zil_commitment_t* cmt = list_head(&(ccf_zil_commitments->zil_blk_commitments));
  if (cmt == NULL) {
    zfs_dbgmsg(" zil_blk_commitments is empty!\n");
    return;
  }
  else {
    #if 0
    zfs_dbgmsg(" cmt->blk_num.zc_word=%016llx:%016llx:%016llx:%016llx\n", \
      (u_longlong_t)cmt->blk_num.zc_word[0], (u_longlong_t)cmt->blk_num.zc_word[1], \
      (u_longlong_t)cmt->blk_num.zc_word[2], (u_longlong_t)cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
    #endif
    for (;;) {
      count++;
      zil_commitment_t* next_cmt = list_next(&(ccf_zil_commitments->zil_blk_commitments), cmt);
      if (next_cmt == NULL) {
        list_remove_head(&(ccf_zil_commitments->zil_blk_commitments));
        free_node(cmt, sizeof(zil_commitment_t));
        zfs_dbgmsg(" [count of commitments in CCF = %d]\n", count);
        return;
      }
      #if 0
      zfs_dbgmsg(" next_cmt->blk_num.zc_word=%016llx:%016llx:%016llx:%016llx\n", \
        (u_longlong_t)next_cmt->blk_num.zc_word[0], (u_longlong_t)next_cmt->blk_num.zc_word[1], \
        (u_longlong_t)next_cmt->blk_num.zc_word[2], (u_longlong_t)next_cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
      #endif
      list_remove_head(&(ccf_zil_commitments->zil_blk_commitments));
      free_node(cmt, sizeof(zil_commitment_t));
      cmt = next_cmt;
    }
  }
  return;
}

__attribute__((unused)) extern void ccf_state_get(ccf_state_t* ccf_zil_commitments) {
  // todo: implement me!
  (void) ccf_zil_commitments;
  zil_commitment_t* cmt = list_head(&(ccf_zil_commitments->zil_blk_commitments));
  if (cmt == NULL) {
    zfs_dbgmsg(" zil_blk_commitments is empty!\n");
    return;
  }
  else {
    #if 0
    zfs_dbgmsg(" cmt->blk_num.zc_word=%016llx:%016llx:%016llx:%016llx\t cmt->blk_digest.zc_word=%016llx:%016llx:%016llx:%016llx\n", \
      (u_longlong_t)cmt->blk_num.zc_word[0], (u_longlong_t)cmt->blk_num.zc_word[1], \
      (u_longlong_t)cmt->blk_num.zc_word[2], (u_longlong_t)cmt->blk_num.zc_word[ZIL_ZC_SEQ], \
      (u_longlong_t)cmt->blk_digest.zc_word[0], (u_longlong_t)cmt->blk_digest.zc_word[1], \
      (u_longlong_t)cmt->blk_digest.zc_word[2], (u_longlong_t)cmt->blk_digest.zc_word[ZIL_ZC_SEQ]);
    for (;;) {
      zil_commitment_t* next_cmt = list_next(&(ccf_zil_commitments->zil_blk_commitments), cmt);
      if (next_cmt == NULL)
        return;

      zfs_dbgmsg(" next_cmt->blk_num.zc_word=%016llx:%016llx:%016llx:%016llx\n", \
        (u_longlong_t)next_cmt->blk_num.zc_word[0], (u_longlong_t)next_cmt->blk_num.zc_word[1], \
        (u_longlong_t)next_cmt->blk_num.zc_word[2], (u_longlong_t)next_cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
      cmt = next_cmt;
    }
    #endif

  } 
  return ;
}


__attribute__((unused)) extern void ccf_state_cmp(ccf_state_t* ccf_zil_commitments,\
   uint64_t * calculated_digest) {
  zil_commitment_t* cmt = list_head(&(ccf_zil_commitments->zil_blk_commitments));
  if (cmt == NULL) {
    zfs_dbgmsg(" zil_blk_commitments is empty!\n");
    return;
  }
  else {
    zfs_dbgmsg(" cmt->blk_num.zc_word=%016llx:%016llx:%016llx:%016llx\n", \
      (u_longlong_t)cmt->blk_num.zc_word[0], (u_longlong_t)cmt->blk_num.zc_word[1], \
      (u_longlong_t)cmt->blk_num.zc_word[2], (u_longlong_t)cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
    if (memcmp(cmt->blk_digest.zc_word, calculated_digest, sizeof(cmt->blk_digest)) == 0) {
      zfs_dbgmsg(" zil_blk_commitments are equal!\n");
      zfs_dbgmsg(" digest=%016llx:%016llx:%016llx:%016llx\t calculated_digest=%016llx:%016llx:%016llx:%016llx\n",\
        (u_longlong_t)cmt->blk_digest.zc_word[0], (u_longlong_t)cmt->blk_digest.zc_word[1], \
        (u_longlong_t)cmt->blk_digest.zc_word[2], (u_longlong_t)cmt->blk_digest.zc_word[3], \
        (u_longlong_t)calculated_digest[0], (u_longlong_t)calculated_digest[1], \
        (u_longlong_t)calculated_digest[2], (u_longlong_t)calculated_digest[3]);
      list_remove(&(ccf_zil_commitments->zil_blk_commitments), cmt);
      free_node(cmt, sizeof(zil_commitment_t));
    }
    else {
      zfs_dbgmsg(" zil_blk_commitments are NOT equal!\n");
      zfs_dbgmsg(" digest=%016llx:%016llx:%016llx:%016llx\t calculated_digest=%016llx:%016llx:%016llx:%016llx\n",\
        (u_longlong_t)cmt->blk_digest.zc_word[0], (u_longlong_t)cmt->blk_digest.zc_word[1], \
        (u_longlong_t)cmt->blk_digest.zc_word[2], (u_longlong_t)cmt->blk_digest.zc_word[3], \
        (u_longlong_t)calculated_digest[0], (u_longlong_t)calculated_digest[1], \
        (u_longlong_t)calculated_digest[2], (u_longlong_t)calculated_digest[3]);
    }
  }
  return ;
}
#endif