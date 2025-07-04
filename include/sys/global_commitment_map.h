#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/map.h>
#include <sys/commitments.h>

extern dyn_array_commitments_t zils_blocks_commitments;

extern kcondvar_t	zil_thread_cv;		/* signalled when "done" */
extern kcondvar_t	ccf_thread_cv;		/* signalled when "done" */
extern kmutex_t	    ccf_lock;	/* protects fields of this struct */
extern kmutex_t	    zil_thread_lock;	/* protects fields of this struct */
extern kmutex_t	    ccf_thread_lock;	/* protects fields of this struct */

extern dyn_array_commitments_t* ccf_zil_header_commitments;
extern dyn_array_commitments_t* ccf_zil_tail_commitments;


extern zil_commitment_t* starting_blk_cmt;
extern zil_commitment_t* final_blk_cmt;


extern int not_initialized;

extern zil_commitment_t zil_tail_commitment;
extern list_t pending_commitments_1; // for the tail
extern list_t pending_commitments_2; // for the tail
extern list_t* consumer_list_handle; // for the tail




extern ccf_state_t ccf_zil_commitments;