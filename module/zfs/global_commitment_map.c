#include <sys/commitments.h>

dyn_array_commitments_t zils_blocks_commitments;
dyn_array_commitments_t* ccf_zil_header_commitments = NULL;
dyn_array_commitments_t* ccf_zil_tail_commitments = NULL;
int not_initialized = 1;
zil_commitment_t zil_tail_commitment;

list_t pending_commitments; // for the tail
list_t* consumer_list_handle; // for the tail

zil_commitment_t* starting_blk_cmt;
zil_commitment_t* final_blk_cmt;

ccf_state_t ccf_zil_commitments;

kcondvar_t	zil_thread_cv;		    /* signalled when "done" */
kcondvar_t	ccf_thread_cv;		/* signalled when "done" */
kmutex_t	ccf_lock;	/* protects fields of this struct */
kmutex_t	zil_thread_lock;	/* protects fields of this struct */
kmutex_t	ccf_thread_lock;	/* protects fields of this struct */