#include <sys/commitments.h>

dyn_array_commitments_t zils_blocks_commitments;
dyn_array_commitments_t* ccf_zil_header_commitments = NULL;
dyn_array_commitments_t* ccf_zil_tail_commitments = NULL;
int not_initialized = 1;
zil_commitment_t zil_tail_commitment;

zil_commitment_t* starting_blk_cmt;
zil_commitment_t* final_blk_cmt;

ccf_state_t ccf_zil_commitments;