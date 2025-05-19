// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/uberblock_impl.h>
#include <sys/vdev_impl.h>
#include <sys/mmp.h>

int
uberblock_verify(uberblock_t *ub)
{
	if (ub->ub_magic == BSWAP_64((uint64_t)UBERBLOCK_MAGIC))
		byteswap_uint64_array(ub, sizeof (uberblock_t));

	if (ub->ub_magic != UBERBLOCK_MAGIC)
		return (SET_ERROR(EINVAL));

	return (0);
}

/*
 * Update the uberblock and return TRUE if anything changed in this
 * transaction group.
 */
boolean_t
uberblock_update(uberblock_t *ub, vdev_t *rvd, uint64_t txg, uint64_t mmp_delay)
{
	ASSERT(ub->ub_txg < txg);

	/*
	 * We explicitly do not set ub_version here, so that older versions
	 * continue to be written with the previous uberblock version.
	 */
	ub->ub_magic = UBERBLOCK_MAGIC;
	ub->ub_txg = txg;
	ub->ub_guid_sum = rvd->vdev_guid_sum;
	ub->ub_timestamp = gethrestime_sec();
	ub->ub_software_version = SPA_VERSION;
	ub->ub_mmp_magic = MMP_MAGIC;
	if (spa_multihost(rvd->vdev_spa)) {
		ub->ub_mmp_delay = mmp_delay;
		ub->ub_mmp_config = MMP_SEQ_SET(0) |
		    MMP_INTERVAL_SET(zfs_multihost_interval) |
		    MMP_FAIL_INT_SET(zfs_multihost_fail_intervals);
	} else {
		ub->ub_mmp_delay = 0;
		ub->ub_mmp_config = 0;
	}
	ub->ub_checkpoint_txg = 0;

	return (BP_GET_LOGICAL_BIRTH(&ub->ub_rootbp) == txg);
}

/*
 * Dump a uberblock
 */
void
uberblock_dump(uberblock_t *ub)
{
        zfs_dbgmsg("ub_print:magic number: %llu\n", (u_longlong_t)ub->ub_magic);
        zfs_dbgmsg("ub_print:version: %llu\n", (u_longlong_t)ub->ub_version);
        zfs_dbgmsg("ub_print:txg: %llu\n", (u_longlong_t)ub->ub_txg);
        zfs_dbgmsg("ub_print:guid sum: %llu\n", (u_longlong_t)ub->ub_guid_sum);
        zfs_dbgmsg("ub_print:timestamp: %llu\n", (u_longlong_t)ub->ub_timestamp);
        // blkptr
        zfs_dbgmsg("ub_print:rootbp->dva[0]: %llu:%llu\n", (u_longlong_t)ub->ub_rootbp.blk_dva[0].dva_word[0], (u_longlong_t)ub->ub_rootbp.blk_dva[0].dva_word[1]);
        zfs_dbgmsg("ub_print:rootbp->dva[0]: %llu:%llu\n", (u_longlong_t)ub->ub_rootbp.blk_dva[1].dva_word[0], (u_longlong_t)ub->ub_rootbp.blk_dva[1].dva_word[1]);
        zfs_dbgmsg("ub_print:rootbp->dva[0]: %llu:%llu\n", (u_longlong_t)ub->ub_rootbp.blk_dva[2].dva_word[0], (u_longlong_t)ub->ub_rootbp.blk_dva[2].dva_word[1]);
        zfs_dbgmsg("ub_print:rootbp->blkprop: %llu\n", (u_longlong_t)ub->ub_rootbp.blk_prop);
        zfs_dbgmsg("ub_print:rootbp->blk_pad: %llu:%llu\n", (u_longlong_t)ub->ub_rootbp.blk_pad[0], (u_longlong_t)ub->ub_rootbp.blk_pad[1]);
        zfs_dbgmsg("ub_print:rootbp->blk_phys_birth: %llu\n", (u_longlong_t)ub->ub_rootbp.blk_birth_word[0]);
        zfs_dbgmsg("ub_print:rootbp->blk_birth: %llu\n", (u_longlong_t)ub->ub_rootbp.blk_birth_word[1]);
        zfs_dbgmsg("ub_print:rootbp->blk_fill: %llu\n", (u_longlong_t)ub->ub_rootbp.blk_fill);
        zfs_dbgmsg("ub_print:rootbp->blk_cksum: %llu:%llu:%llu:%llu\n", (u_longlong_t)ub->ub_rootbp.blk_cksum.zc_word[0], (u_longlong_t)ub->ub_rootbp.blk_cksum.zc_word[1], (u_longlong_t)ub->ub_rootbp.blk_cksum.zc_word[2], (u_longlong_t)ub->ub_rootbp.blk_cksum.zc_word[3]);
        zfs_dbgmsg("ub_print:software_version %llu\n", (u_longlong_t)ub->ub_software_version);
        zfs_dbgmsg("ub_print:mmp magic: %llu\n", (u_longlong_t)ub->ub_mmp_magic);
        zfs_dbgmsg("ub_print:mmp delay: %llu\n", (u_longlong_t)ub->ub_mmp_delay);
        zfs_dbgmsg("ub_print:mmp config: %llu\n", (u_longlong_t)ub->ub_mmp_config);
        zfs_dbgmsg("ub_print:checkpoint txg: %llu\n", (u_longlong_t)ub->ub_checkpoint_txg);
}

/*
 * Seriaze uberblock
 */
void
uberblock_serialize(uberblock_t *ub, uberblock_hex_t *ub_hex)
{
	const uint64_t *ub_data = (const uint64_t *)ub;
	char *ptr = ub_hex->hex_str;
	for (int i = 0; i < UBERBLOCK_SIZE; i ++) {
		ptr += snprintf(ptr, UBERBLOCK_HEX_BUF_SIZE - (ptr - ub_hex->hex_str), "%016llx", (u_longlong_t)ub_data[i]);
	}
	*ptr = '\0';
}

/*
 * Deseriaze uberblock
 */
void
uberblock_deserialize(uberblock_t *ub, uberblock_hex_t *ub_hex)
{
        uint64_t restore_ub[UBERBLOCK_SIZE] = {0};
	char *hex_str = ub_hex->hex_str;
        for (int i = 0; i < UBERBLOCK_SIZE; i ++) {
                uint64_t result = 0;
                for (int j = 0; j < HEX_PER_UINT64; j ++) {
                        uint64_t tmp;
                        int index = i * HEX_PER_UINT64 + j;
                        if (hex_str[index] >= '0' && hex_str[index] <= '9') {
                                tmp = hex_str[index] - '0';
                        }
                        else if (hex_str[index] >= 'a' && hex_str[index] <= 'f') {
                                tmp = hex_str[index] - 'a' + 10;
                        }
                        else {
                                tmp = 0;
                        }
                        result = (result << 4) | tmp;
                }
                restore_ub[i] = result;
        }
	// update uberblock
	memcpy(ub, restore_ub, sizeof(restore_ub));
}

/*
 * Generate sha256 hash digest from serialized uberblock
 */
void
ub_hex_to_digest(uberblock_hex_t *ub_hex, uberblock_digest_t *ub_digest)
{
	SHA2_CTX ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];

	SHA2Init(SHA256, &ctx);
	SHA2Update(&ctx, ub_hex->hex_str, strlen(ub_hex->hex_str));
	SHA2Final(digest, &ctx);

	for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i ++) {
		sprintf(ub_digest->digest + (i * 2), "%02X", digest[i]);
	}
	ub_digest->digest[64] = '\0';
}
