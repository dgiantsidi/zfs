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

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_sb != NULL);
}

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	if (*zfvp == NULL || (*zfvp)->z_sb == NULL ||
	    !atomic_inc_not_zero(&((*zfvp)->z_sb->s_active))) {
		return (SET_ERROR(ESRCH));
	}
	return (0);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	deactivate_super(zfsvfs->z_sb);
}

void
zfsdev_private_set_state(void *priv, zfsdev_state_t *zs)
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

static int zfsdev_map(struct file *filp, struct vm_area_struct *vma) {
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

	zc = vmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	if (ddi_copyin((void *)(uintptr_t)arg, zc, sizeof (zfs_cmd_t), 0)) {
		error = -SET_ERROR(EFAULT);
		goto out;
	}
	error = -zfsdev_ioctl_common(vecnum, zc, 0);
	rc = ddi_copyout(zc, (void *)(uintptr_t)arg, sizeof (zfs_cmd_t), 0);
	if (error == 0 && rc != 0)
		error = -SET_ERROR(EFAULT);
out:
	vmem_free(zc, sizeof (zfs_cmd_t));
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
void
zfs_ioctl_update_mount_cache(const char *dsname)
{
}

void
zfs_ioctl_init_os(void)
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
#define	zfsdev_compat_ioctl	NULL
#endif

static const struct file_operations zfsdev_fops = {
	.open		= zfsdev_open,
	.release	= zfsdev_release,
	.unlocked_ioctl	= zfsdev_ioctl,
	.mmap		= zfsdev_map,
	.compat_ioctl	= zfsdev_compat_ioctl,
	.owner		= THIS_MODULE,
};

static struct miscdevice zfs_misc = {
	.minor		= ZFS_DEVICE_MINOR,
	.name		= ZFS_DRIVER,
	.fops		= &zfsdev_fops,
};

MODULE_ALIAS_MISCDEV(ZFS_DEVICE_MINOR);
MODULE_ALIAS("devname:zfs");

int
zfsdev_attach(void)
{
	int error;

	error = misc_register(&zfs_misc);
	if (error == -EBUSY) {
		/*
		 * Fallback to dynamic minor allocation in the event of a
		 * collision with a reserved minor in linux/miscdevice.h.
		 * In this case the kernel modules must be manually loaded.
		 */
		printk(KERN_INFO "ZFS: misc_register() with static minor %d "
		    "failed %d, retrying with MISC_DYNAMIC_MINOR\n",
		    ZFS_DEVICE_MINOR, error);

		zfs_misc.minor = MISC_DYNAMIC_MINOR;
		error = misc_register(&zfs_misc);
	}

	if (error)
		printk(KERN_INFO "ZFS: misc_register() failed %d\n", error);

	return (error);
}

void
zfsdev_detach(void)
{
	misc_deregister(&zfs_misc);
}

#ifdef ZFS_DEBUG
#define	ZFS_DEBUG_STR	" (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR	""
#endif

zidmap_t *zfs_init_idmap;

#define NETLINK_TEST 17
int thread_id = 0;

struct sock *nl_sock = NULL;

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
static zil_commitment_t prev_tail_cmt;

static void netlink_test_recv_msg(struct sk_buff *skb) {
  struct sk_buff *skb_out;
  struct nlmsghdr *nlh;
  int msg_size;
  char *msg;
  int pid;
  int res;

  nlh = (struct nlmsghdr *)skb->data;
  pid = nlh->nlmsg_pid; /* pid of sending process */
  msg = (char *)nlmsg_data(nlh);
  msg_size = strlen(msg);
  //printk(KERN_INFO "netlink_test: Received request msg_size:%d\n", nlh->nlmsg_len);
  struct userspace_to_kernel_msg* msg_data = decode_received_msg(msg, sizeof(struct userspace_to_kernel_msg));
  //printk(KERN_INFO "netlink_test: Received from request_id: %d, poolname: %s\n", msg_data->request_id, msg_data->poolname);
  zil_commitment_t* tail_cmt = NULL;
  cv_broadcast(&zil_thread_cv);
  for (;;) {
	printk(KERN_INFO "netlink_test: Waiting for tail commitment for pool: %s (request_id: %d)\n", msg_data->poolname, msg_data->request_id);
	mutex_enter(&ccf_lock);
	tail_cmt = get_zil_tail_cmt_for_dsl(msg_data->poolname, ccf_zil_tail_commitments);
	if (tail_cmt == NULL) {
		mutex_exit(&ccf_lock);
		break;
	}
	else if (tail_cmt->blk_num.zc_word[ZIL_ZC_SEQ] == prev_tail_cmt.blk_num.zc_word[ZIL_ZC_SEQ]) {
		printk(KERN_INFO "netlink_test: same_blk_id pool: %s (request_id: %d) block_id=%llu\n", tail_cmt->name, msg_data->request_id, (u_longlong_t)tail_cmt->blk_num.zc_word[3]);
 	    //cv_broadcast(&zil_thread_cv);
		cv_wait(&ccf_thread_cv, &ccf_lock);	
		mutex_exit(&ccf_lock);
	}else {
		prev_tail_cmt = *tail_cmt;
		mutex_exit(&ccf_lock);
		break;
	}
  }
  if (tail_cmt == NULL) {
  	printk(KERN_INFO "netlink_test: Reply for request_id: %d for pool: %s returns NULL\n", msg_data->request_id, msg_data->poolname);
  }
  else {
	printk(KERN_INFO "netlink_test: Reply for request_id: %d for pool: %s for blk %llu\n", msg_data->request_id, msg_data->poolname, (u_longlong_t)tail_cmt->blk_num.zc_word[ZIL_ZC_SEQ]);
  }
 
  /*
   * (0) cv.broadcast() 
   * (1) take lock for commitment
   * (2) get tail commitment for the pool 
	     zil_commitment_t* tail_cmt = get_zil_tail_cmt_for_dsl(msg_data->poolname, ccf_zil_tail_commitments);
   * (3) if tail cmt == prev_tail_cmt	
   * 	(4) cv.wait()
   * (5) respond to user space
   */

  kfree(msg_data);
  //zil_commitment_t* head_cmt = get_zil_head_cmt_for_dsl(msg_data->poolname, ccf_zil_head_commitments);

  // create reply
  skb_out = nlmsg_new(msg_size, 0);
  if (!skb_out) {
    printk(KERN_ERR "netlink_test: Failed to allocate new skb\n");
    return;
  }

  // put received message into reply
  nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
  NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
  strncpy(nlmsg_data(nlh), msg, msg_size);

  // printk(KERN_INFO "netlink_test: Send %s\n", msg);

  res = nlmsg_unicast(nl_sock, skb_out, pid);
  if (res < 0)
    printk(KERN_INFO "netlink_test: Error while sending skb to user\n");
}

static int
openzfs_init_os(void)
{
	int error;

	if ((error = zfs_kmod_init()) != 0) {
		printk(KERN_NOTICE "ZFS: Failed to Load ZFS Filesystem v%s-%s%s"
		    ", rc = %d\n", ZFS_META_VERSION, ZFS_META_RELEASE,
		    ZFS_DEBUG_STR, error);

		return (-error);
	}

	zfs_sysfs_init();

	printk(KERN_NOTICE "ZFS: Loaded module v%s-%s%s, "
	    "ZFS pool version %s, ZFS filesystem version %s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR,
	    SPA_VERSION_STRING, ZPL_VERSION_STRING);
#ifdef HAVE_LINUX_EXPERIMENTAL
	printk(KERN_NOTICE "ZFS: Using ZFS with kernel %s is EXPERIMENTAL and "
	    "SERIOUS DATA LOSS may occur!\n", utsname()->release);
	printk(KERN_NOTICE "ZFS: Please report your results at: "
	    "https://github.com/openzfs/zfs/issues/new\n");
#endif
#ifndef CONFIG_FS_POSIX_ACL
	printk(KERN_NOTICE "ZFS: Posix ACLs disabled by kernel\n");
#endif /* CONFIG_FS_POSIX_ACL */

	zfs_init_idmap = (zidmap_t *)zfs_get_init_idmap();
	printk(KERN_NOTICE "netlink_test: Init module\n");
  	
	cv_init(&zil_thread_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&ccf_thread_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&ccf_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zil_thread_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ccf_thread_lock, NULL, MUTEX_DEFAULT, NULL);



  	struct netlink_kernel_cfg cfg = {
    	.input = netlink_test_recv_msg,
  	};

  	nl_sock = netlink_kernel_create(&init_net, NETLINK_TEST, &cfg);
  	if (!nl_sock) {
    	printk(KERN_NOTICE "netlink_test: Error creating socket.\n");
    	return -10;
  	}
		printk(KERN_NOTICE "netlink_test: Init module success\n");

	return (0);
}

static void
openzfs_fini_os(void)
{
	zfs_sysfs_fini();
	zfs_kmod_fini();
	netlink_kernel_release(nl_sock);
	cv_destroy(&zil_thread_cv);
	cv_destroy(&ccf_thread_cv);
	mutex_destroy(&ccf_lock);
	mutex_destroy(&zil_thread_lock);
	mutex_destroy(&ccf_thread_lock);
	printk(KERN_NOTICE "ZFS: Unloaded module v%s-%s%s\n",
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
MODULE_LICENSE("Dual BSD/GPL"); /* zstd / misc */
MODULE_LICENSE(ZFS_META_LICENSE);
MODULE_VERSION(ZFS_META_VERSION "-" ZFS_META_RELEASE);
