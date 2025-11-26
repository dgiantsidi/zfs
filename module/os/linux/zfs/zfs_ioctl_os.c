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
 * Portions Copyright 2011 Martin Matuska
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Portions Copyright 2012 Pawel Jakub Dawidek <pawel@dawidek.net>
 * Copyright (c) 2014, 2016 Joyent, Inc. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2021 Klara, Inc.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/fm/util.h>
#include <sys/dsl_crypt.h>
#include <sys/crypto/icp.h>
#include <sys/zstd/zstd.h>

#include <sys/zfs_ioctl_impl.h>

#include <sys/zfs_sysfs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>

#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>

#include <sys/netlink_layer.h>
#include <sys/global_commitment_map.h>
#include <sys/zil_impl.h>

#include <sys/config_netlink.h>

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_sb != NULL);
}

int zfs_vfs_ref(zfsvfs_t **zfvp)
{
	if (*zfvp == NULL || (*zfvp)->z_sb == NULL ||
		!atomic_inc_not_zero(&((*zfvp)->z_sb->s_active)))
	{
		return (SET_ERROR(ESRCH));
	}
	return (0);
}

void zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	deactivate_super(zfsvfs->z_sb);
}

void zfsdev_private_set_state(void *priv, zfsdev_state_t *zs)
{
	struct file *filp = priv;

	filp->private_data = zs;
}

zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	zfs_dbgmsg("\n");

	struct file *filp = priv;

	return (filp->private_data);
}

static int
zfsdev_open(struct inode *ino, struct file *filp)
{
	int error;
	zfs_dbgmsg("\n");

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_init(filp);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static int
zfsdev_release(struct inode *ino, struct file *filp)
{
	zfsdev_state_destroy(filp);

	return (0);
}

static int zfsdev_map(struct file *filp, struct vm_area_struct *vma)
{
	zfs_dbgmsg("\n");
#if 0
	int error;
	zfsdev_state_t *zs;

	if (vma->vm_pgoff != 0) {
		return (-SET_ERROR(EINVAL));
	}

	zs = zfsdev_private_get_state(filp);
	if (zs == NULL) {
		return (-SET_ERROR(ENODEV));
	}
#endif

#if 0
	error = zfs_map(zs, vma->vm_start, vma->vm_end - vma->vm_start,
	    vma->vm_flags);
	if (error != 0) {
		return (-error);
	}
#endif

	return (0);
}

static long
zfsdev_ioctl(struct file *filp, unsigned cmd, unsigned long arg)
{
	uint_t vecnum;
	zfs_cmd_t *zc;
	int error, rc;
	zfs_dbgmsg("\n");

	vecnum = cmd - ZFS_IOC_FIRST;

	zc = vmem_zalloc(sizeof(zfs_cmd_t), KM_SLEEP);

	if (ddi_copyin((void *)(uintptr_t)arg, zc, sizeof(zfs_cmd_t), 0))
	{
		error = -SET_ERROR(EFAULT);
		goto out;
	}
	error = -zfsdev_ioctl_common(vecnum, zc, 0);
	rc = ddi_copyout(zc, (void *)(uintptr_t)arg, sizeof(zfs_cmd_t), 0);
	if (error == 0 && rc != 0)
		error = -SET_ERROR(EFAULT);
out:
	vmem_free(zc, sizeof(zfs_cmd_t));
	return (error);
}

static int
zfs_ioc_userns_attach(zfs_cmd_t *zc)
{
	int error;

	if (zc == NULL)
		return (SET_ERROR(EINVAL));

	error = zone_dataset_attach(CRED(), zc->zc_name, zc->zc_cleanup_fd);

	/*
	 * Translate ENOTTY to ZFS_ERR_NOT_USER_NAMESPACE as we just arrived
	 * back from the SPL layer, which does not know about ZFS_ERR_* errors.
	 * See the comment at the user_ns_get() function in spl-zone.c for
	 * details.
	 */
	if (error == ENOTTY)
		error = ZFS_ERR_NOT_USER_NAMESPACE;

	return (error);
}

static int
zfs_ioc_userns_detach(zfs_cmd_t *zc)
{
	int error;

	if (zc == NULL)
		return (SET_ERROR(EINVAL));

	error = zone_dataset_detach(CRED(), zc->zc_name, zc->zc_cleanup_fd);

	/*
	 * See the comment in zfs_ioc_userns_attach() for details on what is
	 * going on here.
	 */
	if (error == ENOTTY)
		error = ZFS_ERR_NOT_USER_NAMESPACE;

	return (error);
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (MIN(ptob(zfs_totalram_pages) / 4, 128 * 1024 * 1024));
}

/* Update the VFS's cache of mountpoint properties */
void zfs_ioctl_update_mount_cache(const char *dsname)
{
}

void zfs_ioctl_init_os(void)
{
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_USERNS_ATTACH,
									 zfs_ioc_userns_attach, zfs_secpolicy_config, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_USERNS_DETACH,
									 zfs_ioc_userns_detach, zfs_secpolicy_config, POOL_CHECK_NONE);
}

#ifdef CONFIG_COMPAT
static long
zfsdev_compat_ioctl(struct file *filp, unsigned cmd, unsigned long arg)
{
	return (zfsdev_ioctl(filp, cmd, arg));
}
#else
#define zfsdev_compat_ioctl NULL
#endif

static const struct file_operations zfsdev_fops = {
	.open = zfsdev_open,
	.release = zfsdev_release,
	.unlocked_ioctl = zfsdev_ioctl,
	.mmap = zfsdev_map,
	.compat_ioctl = zfsdev_compat_ioctl,
	.owner = THIS_MODULE,
};

static struct miscdevice zfs_misc = {
	.minor = ZFS_DEVICE_MINOR,
	.name = ZFS_DRIVER,
	.fops = &zfsdev_fops,
};

MODULE_ALIAS_MISCDEV(ZFS_DEVICE_MINOR);
MODULE_ALIAS("devname:zfs");

int zfsdev_attach(void)
{
	int error;

	error = misc_register(&zfs_misc);
	if (error == -EBUSY)
	{
		/*
		 * Fallback to dynamic minor allocation in the event of a
		 * collision with a reserved minor in linux/miscdevice.h.
		 * In this case the kernel modules must be manually loaded.
		 */
		printk(KERN_INFO "Shielded ZFS w/ acks: misc_register() with static minor %d "
						 "failed %d, retrying with MISC_DYNAMIC_MINOR\n",
			   ZFS_DEVICE_MINOR, error);

		zfs_misc.minor = MISC_DYNAMIC_MINOR;
		error = misc_register(&zfs_misc);
	}

	if (error)
		printk(KERN_INFO "Shielded ZFS w/ acks: misc_register() failed %d\n", error);

	return (error);
}

void zfsdev_detach(void)
{
	misc_deregister(&zfs_misc);
}

#ifdef ZFS_DEBUG
#define ZFS_DEBUG_STR " (DEBUG mode)"
#else
#define ZFS_DEBUG_STR ""
#endif

zidmap_t *zfs_init_idmap;

int thread_id = 0;

struct sock *nl_sock_get_cmts = NULL;
struct sock *nl_sock_notify = NULL;

#if 0
__attribute__((unused)) static struct userspace_to_kernel_msg* decode_received_msg(char* msg, int msg_size) {
	struct userspace_to_kernel_msg* msg_data = kmalloc(sizeof(struct userspace_to_kernel_msg), GFP_KERNEL);
	if (msg_data == NULL) {
		printk(KERN_ERR "netlink_test: Failed to allocate memory for message data\n");
		return NULL;
	}
	int offset = 0;
	//printk(KERN_ERR "netlink_test: Decoding message of size %d, offset %d\n", msg_size, offset);

	memcpy(&(msg_data->request_id), msg+offset, sizeof(msg_data->request_id));
	offset += sizeof(msg_data->request_id);
	memcpy(&(msg_data->req_type), msg+offset, sizeof(msg_data->req_type));
	offset += sizeof(msg_data->req_type);
	msg_size -= offset;
	//printk(KERN_ERR "netlink_test: Decoding message of size %d, offset %d\n", msg_size, offset);
	memcpy(msg_data->poolname, msg+offset, ZFS_MAX_DATASET_NAME_LEN);
	for (int i = 0; i < ZFS_MAX_DATASET_NAME_LEN; i++) {
		if (msg_data->poolname[i] == '\0') {
			break;
		}
		//printk(KERN_INFO "->%c", msg_data->poolname[i]);
	}
	//printk(KERN_ERR "\n->%s\n", msg_data->poolname);

	return msg_data;  
}
#endif

__attribute__((unused)) static get_cmt_msg_t *decode_get_cmt_msg(char *msg, int msg_size)
{
	get_cmt_msg_t *get_cmt = kmalloc(sizeof(get_cmt_msg_t), GFP_KERNEL);
	if (get_cmt == NULL)
	{
		printk(KERN_ERR "decode_get_cmt_msg: Failed to allocate memory for message data\n");
		return NULL;
	}

	if (msg_size != ZFS_MAX_DATASET_NAME_LEN)
	{
		printk(KERN_ERR "decode_get_cmt_msg: Invalid message size %d, expected %d\n", msg_size, ZFS_MAX_DATASET_NAME_LEN);
		kfree(get_cmt);
		return NULL;
	}

	memcpy(get_cmt->poolname, msg, ZFS_MAX_DATASET_NAME_LEN);
	return get_cmt;
}

static zil_commitment_t prev_tail_cmt; // keeps the latest ack-ed commitment

static void notify_cmts_callback(struct sk_buff *skb)
{
	// struct sk_buff *skb_out;

	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
	[[__maybe_unused__]] int pid = nlh->nlmsg_pid; /* pid of sending process */
	char *msg = (char *)nlmsg_data(nlh);
	[[__maybe_unused__]] int msg_size = nlh->nlmsg_len - NLMSG_HDRLEN;

	uint64_t acknowledged_blk_id = 0;
	memcpy(&acknowledged_blk_id, msg, sizeof(uint64_t));
#if 0
  printk(KERN_INFO "notify_cmts (current pid=%d): received notification from pid=%d\
	about blk_id=%lld \
	(payload size=%d)\n", \
	current->pid, pid, acknowledged_blk_id, \
	msg_size);
#endif
	mutex_enter(&ccf_lock);
	int waiters_no = 0;
	for (;;)
	{

		commitments_list_node_t *latest_cmt_node = list_tail(consumer_list_handle);

		if (latest_cmt_node == NULL)
		{
			printk(KERN_INFO "notify_cmts_callback: consumer_list_handle is empty after waking up %d thread(s) with acknowledged_blk_id=%lld (pid=%d, current pid=%d) #############\n",
				   waiters_no, acknowledged_blk_id,  pid, current->pid);
			break;
		}

		zil_commitment_t *latest_cmt = latest_cmt_node->cmt;
		if (latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ] <= acknowledged_blk_id)
		{
			ccf_waiter_t *zcw_ccf_waiter = NULL;
#if 1
			printk(KERN_INFO "notify_cmts_callback: cmt_id=%lld, acknowledged_blk_id=%lld #############\n",\
				latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ], acknowledged_blk_id);
#endif
			while ((zcw_ccf_waiter = list_remove_tail(&(latest_cmt->waiters))) != NULL)
			{
				mutex_enter(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_lock));
				zcw_ccf_waiter->zcw_ccf_ptr->zcw_block_ccf_acked = B_TRUE;
				cv_broadcast(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_cv));
				mutex_exit(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_lock));
				kmem_free(zcw_ccf_waiter, sizeof(ccf_waiter_t));
				waiters_no++;
			}
			list_remove_tail(consumer_list_handle);
			free_node(latest_cmt, sizeof(zil_commitment_t));
			free_node(latest_cmt_node, sizeof(commitments_list_node_t));
		}
		else
		{
			// nothing to process so far
			if (waiters_no > 0)
				printk(KERN_INFO "notify_cmts_callback: returning after waking up %d thread(s) with acknowledged_blk_id=%lld (pid=%d, current pid=%d) #############\n",
				   	waiters_no, acknowledged_blk_id,  pid, current->pid);
			break;
		}
	}
	mutex_exit(&ccf_lock);
}

static char *serialize_recv_cmt(
	const char *poolname, uint64_t blk_id,
	zio_cksum_t tail_commitment)
{
	char *dst_buf = kmalloc(sizeof(recv_cmt_msg_t), GFP_KERNEL);
	memcpy(dst_buf, &(blk_id), sizeof(blk_id));
	memcpy(dst_buf + sizeof(blk_id), poolname,
		   ZFS_MAX_DATASET_NAME_LEN);
	memcpy(dst_buf + sizeof(blk_id) + ZFS_MAX_DATASET_NAME_LEN, tail_commitment.zc_word,
		   sizeof(zio_cksum_t));
	return dst_buf;
}

static void get_cmts_callback(struct sk_buff *skb)
{
	struct sk_buff *skb_out;

	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
	int pid = nlh->nlmsg_pid; /* pid of sending process */
	char *msg = (char *)nlmsg_data(nlh);
	int msg_size = nlh->nlmsg_len;
	char *to_be_copied = NULL;

	get_cmt_msg_t *get_cmt = decode_get_cmt_msg(msg, sizeof(get_cmt_msg_t));
	zil_commitment_t *latest_cmt = NULL;
	commitments_list_node_t *latest_cmt_node = NULL;
#if 0
  printk(KERN_INFO "get_cmts_callback: w/ msg_size=%d from pid=%d, current pid=%d\n",\
	msg_size, pid, current->pid);
#endif
	hrtime_t sleep = 10000; // 10000 nanoseconds = 10 microseconds
	hrtime_t wakeup = gethrtime() + sleep;
	int flag = 0;
	for (;;)
	{
		
		if (mutex_owner(&ccf_lock) == current) {
			// Current thread owns the lock
			printk(KERN_INFO "get_cmts_callback: ERROR: I already hold this lock  (pid=%d, current pid=%d)\n", pid, current->pid);
		}

		mutex_enter(&ccf_lock);
		printk(KERN_INFO "get_cmts_callback (take ccf_lock current pid=%d)\n", current->pid);
		if (consumer_list_handle == NULL || list_is_empty(consumer_list_handle) || list_head(consumer_list_handle) == NULL)
		{
			int rc = -1, iterations = 5e6;
			while (rc == -1)
			{
				wakeup = gethrtime() + sleep;
				rc = cv_timedwait_hires(&ccf_thread_cv,
										&ccf_lock, wakeup, USEC2NSEC(2),
										CALLOUT_FLAG_ABSOLUTE);
				if (rc == -1)
				{
					if (iterations % 100000 == 0)
					{
						printk(KERN_INFO "get_cmts_callback: -- timeout waiting for pool %s, iteration no=%d\n",
							   get_cmt->poolname, iterations);
					}
					ASSERT((&pending_commitments) != NULL);
					consumer_list_handle = &pending_commitments;
					ASSERT(consumer_list_handle != NULL);
					if (!list_is_empty(consumer_list_handle) && list_head(consumer_list_handle) != NULL)
						break;
				}
				iterations--;
				if (iterations <= 0)
				{
					printk(KERN_INFO "get_cmts_callback (release lock): graceful SHUTDOWN for pool %s \n", get_cmt->poolname);
					mutex_exit(&ccf_lock);
					return;
				}
			}
		}
		// printk(KERN_INFO "get_cmts_callback: watchpoint #1\n");
		ASSERT(consumer_list_handle != NULL);
		latest_cmt_node = list_head(consumer_list_handle);
		flag = (!list_is_empty(consumer_list_handle) && latest_cmt_node != NULL);
		if (!list_is_empty(consumer_list_handle) && latest_cmt_node != NULL)
		{
			// printk(KERN_INFO "get_cmts_callback: watchpoint #1.1\n");
			latest_cmt_node = list_head(consumer_list_handle);
			// printk(KERN_INFO "get_cmts_callback: watchpoint #1.2\n");
			if (latest_cmt_node == NULL) {
				printk(KERN_INFO "get_cmts_callback: ERROR: latest_cmt_node is NULL\n");
			}
			if (latest_cmt_node->cmt == NULL) {
				printk(KERN_INFO "get_cmts_callback: ERROR: latest_cmt_node->cmt is NULL\n");
			}
			latest_cmt = latest_cmt_node->cmt;
			// printk(KERN_INFO "get_cmts_callback: watchpoint #1.3\n");
			if (prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ] == -1)
			{
				to_be_copied = serialize_recv_cmt(get_cmt->poolname, latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ],
												  latest_cmt->blk_digest);
				prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ] = latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ];
				
				mutex_exit(&ccf_lock);
				printk(KERN_INFO "get_cmts_callback (release lock): this only happens once ..\n");
				break;
			}
			else
			{
				printk(KERN_INFO "get_cmts_callback: watchpoint #1.4\n");
				while (latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ] == prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ])
				{
					// printk(KERN_INFO "get_cmts_callback: latest_cmt equals prev_tail_cmt ..\n");
					int rc = -1, iterations = 5e6;
					while (rc == -1)
					{
						wakeup = gethrtime() + sleep;
						rc = cv_timedwait_hires(&ccf_thread_cv,
												&ccf_lock, wakeup, USEC2NSEC(2),
												CALLOUT_FLAG_ABSOLUTE);
						if (rc == -1)
						{
							if (iterations % 100000 == 0)
							{
								printk(KERN_INFO "get_cmts_callback: watchpoint #2: timeout.. waiting for pool %s, iteration no=%d\n",
									   get_cmt->poolname, iterations);
							}
						}
						iterations--;
						if (iterations <= 0)
						{
							mutex_exit(&ccf_lock);
							printk(KERN_INFO "get_cmts_callback (release ccf_lock): graceful SHUTDOWN for pool %s \n", get_cmt->poolname);
							return;
						}
						latest_cmt_node = list_head(consumer_list_handle);
						if (latest_cmt_node != NULL)
						{
							latest_cmt = latest_cmt_node->cmt;

							if (latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ] != prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ]) {
								printk(KERN_INFO "get_cmts_callback: rc=%d, got new cmt to send: latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ]=%llu current pid=%d\n", 
									rc, (u_longlong_t)latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ], current->pid);
								break;
							}
						}
						else
						{
							//printk(KERN_INFO "get_cmts_callback: latest_cmt_node is NULL, exiting ..\n");
							rc = -1;
						}
					}
					// printk(KERN_INFO "get_cmts_callback: watchpoint #2.1 rc=%d\n", rc);
					
					break;
				}
				// printk(KERN_INFO "get_cmts_callback: watchpoint #3\n");
				latest_cmt_node = list_head(consumer_list_handle);
				if (latest_cmt_node == NULL) {
					printk(KERN_INFO "get_cmts_callback: ERROR: latest_cmt_node is NULL after waking up\n");
				}
				latest_cmt = latest_cmt_node->cmt;
				to_be_copied = serialize_recv_cmt(get_cmt->poolname, latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ],
												  latest_cmt->blk_digest);
				prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ] = latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ];
				printk(KERN_INFO "get_cmts_callback (release ccf_lock): To send zil_blk_id=%llu current pid=%d\n", (u_longlong_t)latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ], current->pid);
				mutex_exit(&ccf_lock);
			}

			break;
		}
	}
  printk(KERN_INFO "get_cmts_callback: current pid=%d (flag=%d, to_be_copied=%d (should be 1))\n", current->pid, flag, to_be_copied == NULL ? 0 : 1);
#if 0

  printk(KERN_INFO "get_cmts_callback: ---- latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ]=%llu\n", \
	(u_longlong_t)latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
#endif

	// create reply

	// printk(KERN_INFO "Allocating skb with size: %d\n", msg_size);

	if (in_atomic())
	{
		printk(KERN_WARNING "Called in atomic context\n");
	}

	skb_out = nlmsg_new(msg_size, 0);
	if (!skb_out)
	{
		printk(KERN_ERR "get_cmts_callback: failed to allocate new skb size=%dB\n", msg_size);
		return;
	}
	// printk(KERN_INFO "get_cmts_callback: 7\n");
	// put received message into reply
	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */

	//printk(KERN_INFO "get_cmts_callback: watchpoint #4\n");

	memcpy(nlmsg_data(nlh), to_be_copied, get_size_of_recv_cmt());
	kfree(to_be_copied);
	kfree(get_cmt);
	//printk(KERN_INFO "get_cmts_callback: watchpoint #5\n");

	int res = nlmsg_unicast(nl_sock_get_cmts, skb_out, pid);
	if (res < 0)
		printk(KERN_INFO "get_cmts_callback: error while sending skb to pid=%d\n", pid);
	//printk(KERN_INFO "get_cmts_callback: watchpoint #6\n");
}

#if 0 // this is the old callback function, which is not used anymore
static void get_cmts_callback(struct sk_buff *skb) {
  struct sk_buff *skb_out;  
  int res;
  

  struct nlmsghdr* nlh = (struct nlmsghdr *)skb->data;
  int pid = nlh->nlmsg_pid; /* pid of sending process */
  char* msg = (char *)nlmsg_data(nlh);
  int msg_size = nlh->nlmsg_len - HNLMSG_HDRLEN;
  
 // printk(KERN_INFO "netlink_test: Received request msg_size:%d %d\n", nlh->nlmsg_len, msg_size);
  struct userspace_to_kernel_msg* msg_data = decode_received_msg(msg, sizeof(struct userspace_to_kernel_msg));
  //printk(KERN_INFO "recv_cmt_callback: received acked cmt for block id: %d, poolname: %s\n", msg_data->request_id, msg_data->poolname);
  zil_commitment_t* latest_cmt = NULL;
  list_t* consumer_list = NULL;
  hrtime_t sleep = 10000; // 10000 nanoseconds = 10 microseconds
  hrtime_t wakeup = gethrtime() + sleep;
  
  for (;;) {
	//printk(KERN_INFO "recv_cmt_callback: waiting to get tail commitment for pool: %s (last acked cmt: %d)\n", msg_data->poolname, msg_data->request_id);
	mutex_enter(&ccf_lock);
	//latest_cmt = &zil_tail_commitment;// get_zil_tail_cmt_for_dsl(msg_data->poolname, ccf_zil_tail_commitments);
	if (consumer_list_handle == NULL) {
		// printk(KERN_INFO "netlink_test: consumer_list_handle is empty(NULL) for pool: %s (request_id: %d)\n", msg_data->poolname, msg_data->request_id);
		int rc = -1,  iterations = 10e6;
		//cv_wait(&ccf_thread_cv, &ccf_lock);	
		while (rc == -1) {
			wakeup = gethrtime() + sleep;
			rc = cv_timedwait_hires(&ccf_thread_cv,
					&ccf_lock, wakeup, USEC2NSEC(2),
					CALLOUT_FLAG_ABSOLUTE);
			if (rc == -1) {
				if (iterations%100000 == 0) {
					printk(KERN_INFO "recv_cmt_callback: timeout waiting for pool: %s (request_id: %d) iteration no=%d\n", \
						msg_data->poolname, msg_data->request_id, iterations);
				}
				consumer_list_handle = (consumer_list_handle == &pending_commitments) ? &pending_commitments : &pending_commitments;
				if (!list_is_empty(consumer_list_handle))
					break;
			}
			iterations--;
			if (iterations <= 0) {
				printk(KERN_INFO "recv_cmt_callback: graceful shutdown for pool: %s (request_id: %d)\n", msg_data->poolname, msg_data->request_id);
				mutex_exit(&ccf_lock);
				return;
			}
		}
		mutex_exit(&ccf_lock);
		break;
	}
	else {
		consumer_list = consumer_list_handle;
	}
	//if (latest_cmt == NULL)
	if (list_is_empty(consumer_list)){
		//cv_wait(&ccf_thread_cv, &ccf_lock);	
		int rc = -1, iterations = 10e6;
#if 0
		printk(KERN_INFO "netlink_test: consumer_list=%p for pool: %s (request_id: %d)\n", \
				(void*) consumer_list, \
				msg_data->poolname, msg_data->request_id);
#endif	
		while (rc == -1) {
			wakeup = gethrtime() + sleep;
			rc = cv_timedwait_hires(&ccf_thread_cv,
					&ccf_lock, wakeup, USEC2NSEC(10),
					CALLOUT_FLAG_ABSOLUTE);
			if (!list_is_empty(consumer_list_handle))
				break;
			else if (rc == -1) {
				if (iterations%100000 == 0) {
					printk(KERN_INFO "recv_cmt_callback: timeout waiting for pool: %s (request_id: %d) iteration no=%d\n", \
						msg_data->poolname, msg_data->request_id, iterations);
				}
				consumer_list_handle = (consumer_list_handle == &pending_commitments) ? &pending_commitments : &pending_commitments;
				if (!list_is_empty(consumer_list_handle))
					break;
			}
			iterations--;
			if (iterations <= 0) {
				printk(KERN_INFO "recv_cmt_callback: graceful shutdown for pool: %s (request_id: %d)\n", msg_data->poolname, msg_data->request_id);
				mutex_exit(&ccf_lock);
				return;
			}
		}
		mutex_exit(&ccf_lock);
		break;
	}
	else {
		latest_cmt = list_tail(consumer_list);
		if (latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ] == prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ]) {
#if 1
			printk(KERN_INFO "netlink_test (it should not be here --- reading from consumer_list=%p): same_blk_id pool: %s (request_id: %d) block_id=%llu\n", \
				(void*) consumer_list, \
				latest_cmt->name, msg_data->request_id, (u_longlong_t)latest_cmt->blk_num.zc_word[3]);
#endif
			//cv_broadcast(&zil_thread_cv);
			int rc = -1;
			//cv_wait(&ccf_thread_cv, &ccf_lock);	
		
			while (rc == -1) {
				wakeup = gethrtime() + sleep;
				rc = cv_timedwait_hires(&ccf_thread_cv,
					&ccf_lock, wakeup, USEC2NSEC(2),
					CALLOUT_FLAG_ABSOLUTE);
				if (!list_is_empty(consumer_list_handle))
					break;
			}
#if 0
			printk(KERN_INFO "netlink_test (reading from consumer_list=%p): woken up pool: %s (request_id: %d) block_id=%llu\n", \
				(void*) consumer_list, \
				latest_cmt->name, msg_data->request_id, (u_longlong_t)latest_cmt->blk_num.zc_word[3]);
#endif
			mutex_exit(&ccf_lock);
#if 0
			printk(KERN_INFO "netlink_test (reading from consumer_list=%p): exiting the mtx pool: %s (request_id: %d) block_id=%llu\n", \
				(void*) consumer_list, \
				latest_cmt->name, msg_data->request_id, (u_longlong_t)latest_cmt->blk_num.zc_word[3]);
#endif
		} 
		else {
			int iteration = 0;
			for (;;) {
				if (latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ] <= msg_data->request_id) {
					prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ] = latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ];
					ccf_waiter_t* zcw_ccf_waiter = NULL;
#if 0
					printk(KERN_INFO "netlink_test (reading from consumer_list=%p): about to update the pool: %s (request_id: %d) block_id=%llu\n", \
						(void*) consumer_list, \
						latest_cmt->name, msg_data->request_id, (u_longlong_t)latest_cmt->blk_num.zc_word[3]);
#endif

					while ((zcw_ccf_waiter = list_remove_tail(&(latest_cmt->waiters))) != NULL) {
#if 0
						printk(KERN_INFO "netlink_test (reading from consumer_list=%p): after getting the zwc handle: %s (request_id: %d) block_id=%llu zcw=%p and *zcw=%p\n", \
							(void*) consumer_list, \
							latest_cmt->name, msg_data->request_id, (u_longlong_t)latest_cmt->blk_num.zc_word[3], (void*)zcw_ccf_waiter,(void*)(zcw_ccf_waiter));
						
						printk(KERN_INFO "netlink_test: here on blk_id=%llu\n", zcw_ccf_waiter->zcw_ccf_ptr->zcw_block_id);
#endif
						mutex_enter(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_lock));
#if 0
						printk(KERN_INFO "netlink_test: wake up waiter on blk_id=%llu  iteration=%d\n",\
							zcw_ccf_waiter->zcw_ccf_ptr->zcw_block_id, iteration);
#endif
						zcw_ccf_waiter->zcw_ccf_ptr->zcw_block_ccf_acked = B_TRUE;
						cv_broadcast(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_cv));
						mutex_exit(&(zcw_ccf_waiter->zcw_ccf_ptr->zcw_ccf_lock));
						kmem_free(zcw_ccf_waiter, sizeof (ccf_waiter_t));
					}
					list_remove_tail(consumer_list);
					free_node(latest_cmt, sizeof(zil_commitment_t));
					latest_cmt = list_tail(consumer_list);
					iteration++;
					// printk(KERN_INFO "netlink_test (reading from consumer_list=%p)\n", (void*) consumer_list);

					if (latest_cmt == NULL) {
						msg_data->request_id = prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ];
						printk(KERN_INFO "get_cmts_callback: acknowledged block ids up to %d\n", msg_data->request_id);
						break;
					}
				}
				else {
					latest_cmt = list_head(consumer_list);
					msg_data->request_id = latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ];
					printk(KERN_INFO "get_cmts_callback: latest cmt to be acknowledged is for block id=%d\n", msg_data->request_id);
					break;

				}	
			}
		}
		mutex_exit(&ccf_lock);
		break;
	}
  }
#if 0
  if (latest_cmt == NULL) {
  	printk(KERN_INFO "netlink_test: Reply for request_id: %d for pool: %s returns NULL\n", msg_data->request_id, msg_data->poolname);
  }
  else {
	printk(KERN_INFO "netlink_test: Reply for request_id: %d for pool: %s for blk %llu\n", msg_data->request_id, msg_data->poolname, (u_longlong_t)latest_cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
	}
#endif
 
  // create reply
  skb_out = nlmsg_new(msg_size, 0);
  if (!skb_out) {
    printk(KERN_ERR "get_cmts_callback: failed to allocate new skb\n");
    return;
  }

  // put received message into reply
  nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
  NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
  memcpy(msg, &(msg_data->request_id), sizeof(int));
 

  memcpy(nlmsg_data(nlh), &(msg_data->request_id), sizeof(int));
  kfree(msg_data);
  int test = 0;
  memcpy(&test, nlmsg_data(nlh), sizeof(int));
  struct userspace_to_kernel_msg* msg_data_dbg = decode_received_msg(nlmsg_data(nlh), sizeof(struct userspace_to_kernel_msg));
  //printk(KERN_INFO "recv_cmt_callback: about to send request_id: %d %d \n", test, msg_data_dbg->request_id);
  kfree(msg_data_dbg);

  //printk(KERN_INFO "netlink_test: Send %d %s\n", msg_data->request_id, (char*) nlmsg_data(nlh));
  

  int res = nlmsg_unicast(nl_sock_get_cmts, skb_out, pid);
  if (res < 0)
    printk(KERN_INFO "get_cmts_callback: error while sending skb to pid=%d\n", pid);
}
#endif

static int
openzfs_init_os(void)
{
	int error;

	if ((error = zfs_kmod_init()) != 0)
	{
		printk(KERN_NOTICE "Shielded ZFS w/ acks: Failed to Load ZFS Filesystem v%s-%s%s"
						   ", rc = %d\n",
			   ZFS_META_VERSION, ZFS_META_RELEASE,
			   ZFS_DEBUG_STR, error);

		return (-error);
	}

	zfs_sysfs_init();

	printk(KERN_NOTICE "Shielded ZFS w/ acked commitments: Loaded module v%s-%s%s, "
					   "ZFS pool version %s, ZFS filesystem version %s\n",
		   ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR,
		   SPA_VERSION_STRING, ZPL_VERSION_STRING);
#ifdef HAVE_LINUX_EXPERIMENTAL
	printk(KERN_NOTICE "Shielded ZFS w/ acks: Using ZFS with kernel %s is EXPERIMENTAL and "
					   "SERIOUS DATA LOSS may occur!\n",
		   utsname()->release);
	printk(KERN_NOTICE "Shielded ZFS w/ acks: Please report your results at: "
					   "https://github.com/openzfs/zfs/issues/new\n");
#endif
#ifndef CONFIG_FS_POSIX_ACL
	printk(KERN_NOTICE "Shielded ZFS w/ acks: Posix ACLs disabled by kernel\n");
#endif /* CONFIG_FS_POSIX_ACL */

	zfs_init_idmap = (zidmap_t *)zfs_get_init_idmap();
	printk(KERN_NOTICE "Shielded ZFS w/ acks: sockets initialization ...\n");

	cv_init(&zil_thread_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&ccf_thread_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&ccf_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zil_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ccf_thread_lock, NULL, MUTEX_DEFAULT, NULL);

	struct netlink_kernel_cfg get_cmts_cfg = {
		.input = get_cmts_callback,
	};

	struct netlink_kernel_cfg cfg_notify_cmts = {
		.input = notify_cmts_callback,
	};
	prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ] = -1;
	nl_sock_get_cmts = netlink_kernel_create(&init_net, GET_CMTS_SOCK, &get_cmts_cfg);
	if (!nl_sock_get_cmts)
	{
		printk(KERN_NOTICE "Shielded ZFS w/ acks: error creating socket for getting cmts.\n");
		return (-1);
	}

	nl_sock_notify = netlink_kernel_create(&init_net, NOTIFY_CMTS_SOCK, &cfg_notify_cmts);
	if (!nl_sock_notify)
	{
		printk(KERN_NOTICE "Shielded ZFS w/ acks: error creating socket for notifying/receiving cmts.\n");
		return (-1);
	}
	printk(KERN_NOTICE "Shielded ZFS w/ acks: sockets initialization is successful ..\n");
	return (0);
}

static void
openzfs_fini_os(void)
{
	zfs_sysfs_fini();
	zfs_kmod_fini();
	netlink_kernel_release(nl_sock_get_cmts);
	netlink_kernel_release(nl_sock_notify);

	cv_destroy(&zil_thread_cv);
	cv_destroy(&ccf_thread_cv);
	mutex_destroy(&ccf_lock);
	mutex_destroy(&zil_thread_lock);
	mutex_destroy(&ccf_thread_lock);
	printk(KERN_NOTICE "Shielded ZFS w/ acks: Unloaded module v%s-%s%s\n",
		   ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR);
}

extern int __init zcommon_init(void);
extern void zcommon_fini(void);

static int __init
openzfs_init(void)
{
	int err;
	if ((err = zcommon_init()) != 0)
		goto zcommon_failed;
	if ((err = icp_init()) != 0)
		goto icp_failed;
	if ((err = zstd_init()) != 0)
		goto zstd_failed;
	if ((err = openzfs_init_os()) != 0)
		goto openzfs_os_failed;
	return (0);

openzfs_os_failed:
	zstd_fini();
zstd_failed:
	icp_fini();
icp_failed:
	zcommon_fini();
zcommon_failed:
	return (err);
}

static void __exit
openzfs_fini(void)
{
	openzfs_fini_os();
	zstd_fini();
	icp_fini();
	zcommon_fini();
}

#if defined(_KERNEL)
module_init(openzfs_init);
module_exit(openzfs_fini);
#endif

MODULE_ALIAS("zavl");
MODULE_ALIAS("icp");
MODULE_ALIAS("zlua");
MODULE_ALIAS("znvpair");
MODULE_ALIAS("zunicode");
MODULE_ALIAS("zcommon");
MODULE_ALIAS("zzstd");
MODULE_DESCRIPTION("ZFS");
MODULE_AUTHOR(ZFS_META_AUTHOR);
MODULE_LICENSE("Dual MIT/GPL"); /* lua */
MODULE_LICENSE("Dual BSD/GPL"); /* zstd */