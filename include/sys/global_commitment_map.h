#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/map.h>
#include <sys/commitments.h>

// extern dyn_array_commitments_t zils_blocks_commitments;

// extern dyn_array_commitments_t *ccf_zil_header_commitments;
// extern dyn_array_commitments_t *ccf_zil_tail_commitments;

// these are for recovery purposes
extern zil_commitment_t *starting_blk_cmt;
extern zil_commitment_t *final_blk_cmt;

extern int not_initialized;

extern zil_commitment_t zil_tail_commitment;
extern zil_commitment_t zil_header_commitment;

