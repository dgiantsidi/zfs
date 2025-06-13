#include <sys/zfs_context.h>

#define HEX_PER_UINT8_SZ sizeof(int)
#define ZIL_COMMITMENT_SIZE SHA256_DIGEST_LENGTH * HEX_PER_UINT8_SZ + 1 /* end-of-array */

enum request_type {
	REQUEST_TYPE_GET_COMMITMENT = 0,
	REQUEST_TYPE_NOTIFY_ZIL
};

struct userspace_to_kernel_msg {
	int request_id; // Unique ID for the request
	int req_type;
	char poolname[ZFS_MAX_DATASET_NAME_LEN];
	char digest[ZIL_COMMITMENT_SIZE]; // to be calculated on the serialized zil_header data
	zio_cksum_t blk_num;
};
