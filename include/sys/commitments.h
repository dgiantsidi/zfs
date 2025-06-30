#pragma once
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/abd.h>
#include <zfs_fletcher.h>
#include <sys/sha2.h>

#define HEX_PER_UINT8_SZ sizeof(int)
#define ZIL_COMMITMENT_SIZE SHA256_DIGEST_LENGTH * HEX_PER_UINT8_SZ + 1 /* end-of-array */

enum commitments_type {
	ZIL_TAIL_COMMITMENT = 0,
	ZIL_HEAD_COMMITMENT
};

struct zil_commitment {
	char digest[ZIL_COMMITMENT_SIZE]; // to be calculated on the serialized zil_header data

	/* other data maybe for debugging*/
	uint64_t txg_sync;
	zio_cksum_t blk_num;
	dva_t allocated_bp;
	char name[ZFS_MAX_DATASET_NAME_LEN];
	zio_cksum_t blk_digest;
	list_t waiters;
};

typedef struct zil_commitment zil_commitment_t;

struct dyn_array_commitments {
	/* 
	 * the number of actual ZIL commitments (one per ZIL in the pool)
	 * Usually we have 3; MOS-ZIL, pool-level ZIL and pool/fs level ZIL (MOS + one per dataset/objset)
	 */

	int count;

	zil_commitment_t* cmt_data;
	struct dyn_array_commitments* next;
};

typedef struct dyn_array_commitments dyn_array_commitments_t;


__attribute__((unused)) extern zil_commitment_t* dump_zil_commitment(const zil_header_t* zh, const char* name, uint64_t txg);
__attribute__((unused)) extern zil_commitment_t* generate_zil_header_cmt(const zil_header_t* zh, const char* name, uint64_t txg);
__attribute__((unused)) extern void append_cmts(dyn_array_commitments_t* zils_header_commitments, zil_commitment_t* zil_header_cmt);
#if 1
__attribute__((unused)) extern void ccf_zil_commitments_protocol(dyn_array_commitments_t* zils_header_commitments, \
	dyn_array_commitments_t* zils_tail_commitments, zil_commitment_t* zil_tail_cmt);
#endif
__attribute__((unused)) extern void cleanup_cmts(dyn_array_commitments_t* zils_header_commitments);
__attribute__((unused)) extern void dump_zil_commitment_from_global_state(const dyn_array_commitments_t* zil_cmt);
__attribute__((unused)) extern zil_commitment_t* generate_zil_tail_cmt(const char* name, uint64_t txg, const zio_cksum_t blk_cksum, dva_t* allocated_bp);
__attribute__((unused)) extern void dump_zil_commitment2(const zil_commitment_t* cmt);
__attribute__((unused)) extern void ccf_commit_cmts(dyn_array_commitments_t* zils_header_commitments, enum commitments_type is_tail);
__attribute__((unused)) extern void copy_commitments(dyn_array_commitments_t* dst, dyn_array_commitments_t src);
__attribute__((unused)) extern void cleanup_ccf_cmts(dyn_array_commitments_t* prev_ccf_cmts);
__attribute__((unused)) dyn_array_commitments_t* copy_commitments2(dyn_array_commitments_t* dst, dyn_array_commitments_t src);
__attribute__((unused)) zil_commitment_t*  get_zil_header_cmt_for_dsl(const char* name, dyn_array_commitments_t* ccf_zil_header_commitments);
__attribute__((unused)) zil_commitment_t* get_zil_tail_cmt_for_dsl(const char* name, dyn_array_commitments_t* ccf_zil_header_commitments);



// 2nd idea
struct ccf_state {
	char name[ZFS_MAX_DATASET_NAME_LEN]; 
	list_t zil_blk_commitments;
};

typedef struct ccf_state ccf_state_t;

__attribute__((unused)) extern void ccf_state_init(ccf_state_t* ccf_zil_commitments);
__attribute__((unused)) extern void ccf_state_append(ccf_state_t* ccf_zil_commitments, zil_commitment_t* zil_cmt);
__attribute__((unused)) extern void ccf_state_cleanup(ccf_state_t* ccf_zil_commitments);
__attribute__((unused)) extern void ccf_state_get(ccf_state_t* ccf_zil_commitments);
__attribute__((unused)) extern void ccf_state_cmp(ccf_state_t* ccf_zil_commitments, uint64_t* calulated_digest);

//__attribute__((unused)) zil_commitment_t*  get_zil_header_cmt_for_dsl(ccf_state_t* ccf_zil_commitments);
//__attribute__((unused)) zil_commitment_t* get_zil_tail_cmt_for_dsl(ccf_state_t* ccf_zil_commitments);

