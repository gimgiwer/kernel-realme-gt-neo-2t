#include <linux/version.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/printk.h>
#include <linux/namei.h>
#include <linux/list.h>
#include <linux/init_task.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/fdtable.h>
#include <linux/statfs.h>
#include <linux/susfs.h>
#include "mount.h"

static DEFINE_SPINLOCK(susfs_spin_lock);

extern bool susfs_is_current_ksu_domain(void);
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
extern void ksu_try_umount(const char *mnt, bool check_mnt, int flags, uid_t uid);
#endif

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
bool susfs_is_log_enabled __read_mostly = false;
#define SUSFS_LOGI(fmt, ...) if (susfs_is_log_enabled) pr_info("susfs:[%u][%d][%s] " fmt, current_uid().val, current->pid, __func__, ##__VA_ARGS__)
#define SUSFS_LOGE(fmt, ...) if (susfs_is_log_enabled) pr_err("susfs:[%u][%d][%s]" fmt, current_uid().val, current->pid, __func__, ##__VA_ARGS__)
#else
#define SUSFS_LOGI(fmt, ...) 
#define SUSFS_LOGE(fmt, ...) 
#endif

/* sus_path */
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
static DEFINE_HASHTABLE(SUS_PATH_HLIST, 10);
static int susfs_update_sus_path_inode(char *target_pathname) {
	struct path p;
	struct inode *inode = NULL;
	const char *dev_type;

	if (kern_path(target_pathname, LOOKUP_FOLLOW, &p)) {
		SUSFS_LOGE("Failed opening file '%s'\n", target_pathname);
		return 1;
	}

	// - We don't allow paths of which filesystem type is "tmpfs" or "fuse".
	//   For tmpfs, because its starting inode->i_ino will begin with 1 again,
	//   so it will cause wrong comparison in function susfs_sus_ino_for_filldir64()
	//   For fuse, which is almost storage related, sus_path should not handle any paths of
	//   which filesystem is "fuse" as well, since app can write to "fuse" and lookup files via
	//   like binder / system API (you can see the uid is changed to 1000)/
	// - so sus_path should be applied only on read-only filesystem like "erofs" or "f2fs", but not "tmpfs" or "fuse",
	//   people may rely on HMA for /data isolation instead.
	dev_type = p.mnt->mnt_sb->s_type->name;
	if (!strcmp(dev_type, "tmpfs") ||
		!strcmp(dev_type, "fuse")) {
		SUSFS_LOGE("target_pathname: '%s' cannot be added since its filesystem type is '%s'\n",
						target_pathname, dev_type);
		path_put(&p);
		return 1;
	}

	inode = d_inode(p.dentry);
	if (!inode) {
		SUSFS_LOGE("inode is NULL\n");
		path_put(&p);
		return 1;
	}

	if (!(inode->i_state & INODE_STATE_SUS_PATH)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= INODE_STATE_SUS_PATH;
		spin_unlock(&inode->i_lock);
	}
	path_put(&p);
	return 0;
}

static int susfs_add_sus_path_internal(unsigned long target_ino, const char *target_pathname) {
	struct st_susfs_sus_path_hlist *new_entry, *tmp_entry;
	struct hlist_node *tmp_node;
	int bkt;
	bool update_hlist = false;

	spin_lock(&susfs_spin_lock);
	hash_for_each_safe(SUS_PATH_HLIST, bkt, tmp_node, tmp_entry, node) {
		if (!strcmp(tmp_entry->target_pathname, target_pathname)) {
			hash_del_rcu(&tmp_entry->node);
			kfree_rcu(tmp_entry, rcu);
			update_hlist = true;
			break;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_entry = kmalloc(sizeof(struct st_susfs_sus_path_hlist), GFP_KERNEL);
	if (!new_entry) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

	new_entry->target_ino = target_ino;
	strncpy(new_entry->target_pathname, target_pathname, SUSFS_MAX_LEN_PATHNAME - 1);
	new_entry->target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	if (susfs_update_sus_path_inode(new_entry->target_pathname)) {
		kfree(new_entry);
		return 1;
	}
	spin_lock(&susfs_spin_lock);
	hash_add_rcu(SUS_PATH_HLIST, &new_entry->node, target_ino);
	if (update_hlist) {
		SUSFS_LOGI("target_ino: '%lu', target_pathname: '%s' is successfully updated to SUS_PATH_HLIST\n",
				new_entry->target_ino, new_entry->target_pathname);	
	} else {
		SUSFS_LOGI("target_ino: '%lu', target_pathname: '%s' is successfully added to SUS_PATH_HLIST\n",
				new_entry->target_ino, new_entry->target_pathname);
	}
	spin_unlock(&susfs_spin_lock);
	return 0;
}

int susfs_add_sus_path(struct st_susfs_sus_path* __user user_info) {
	struct st_susfs_sus_path info;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

	return susfs_add_sus_path_internal(info.target_ino, info.target_pathname);
}

int susfs_sus_ino_for_filldir64(unsigned long ino) {
	struct st_susfs_sus_path_hlist *entry;

	rcu_read_lock();
	hash_for_each_possible_rcu(SUS_PATH_HLIST, entry, node, ino) {
		if (entry->target_ino == ino) {
			rcu_read_unlock();
			return 1;
		}
	}
	rcu_read_unlock();
	return 0;
}
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_PATH

/* sus_mount */
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
static LIST_HEAD(LH_SUS_MOUNT);
static void susfs_update_sus_mount_inode(char *target_pathname) {
	struct mount *mnt = NULL;
	struct path p;
	struct inode *inode = NULL;
	int err = 0;

	err = kern_path(target_pathname, LOOKUP_FOLLOW, &p);
	if (err) {
		SUSFS_LOGE("Failed opening file '%s'\n", target_pathname);
		return;
	}

	/* It is important to check if the mount has a legit peer group id, if so we cannot add them to sus_mount,
	 * since there are chances that the mount is a legit mountpoint, and it can be misued by other susfs functions in future.
	 * And by doing this it won't affect the sus_mount check as other susfs functions check by mnt->mnt_id
	 * instead of INODE_STATE_SUS_MOUNT.
	 */
	mnt = real_mount(p.mnt);
	if (mnt->mnt_group_id > 0 && // 0 means no peer group
		mnt->mnt_group_id < DEFAULT_SUS_MNT_GROUP_ID) {
		SUSFS_LOGE("skip setting SUS_MOUNT inode state for path '%s' since its source mount has a legit peer group id\n", target_pathname);
		path_put(&p);
		return;
	}

	inode = d_inode(p.dentry);
	if (!inode) {
		path_put(&p);
		SUSFS_LOGE("inode is NULL\n");
		return;
	}

	if (!(inode->i_state & INODE_STATE_SUS_MOUNT)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= INODE_STATE_SUS_MOUNT;
		spin_unlock(&inode->i_lock);
	}
	path_put(&p);
}

int susfs_add_sus_mount(struct st_susfs_sus_mount* __user user_info) {
	struct st_susfs_sus_mount_list *cursor = NULL, *temp = NULL;
	struct st_susfs_sus_mount_list *new_list = NULL;
	struct st_susfs_sus_mount info;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
#ifdef CONFIG_MIPS
	info.target_dev = new_decode_dev(info.target_dev);
#else
	info.target_dev = huge_decode_dev(info.target_dev);
#endif /* CONFIG_MIPS */
#else
	info.target_dev = old_decode_dev(info.target_dev);
#endif /* defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64) */

	spin_lock(&susfs_spin_lock);
	list_for_each_entry_safe(cursor, temp, &LH_SUS_MOUNT, list) {
		if (unlikely(!strcmp(cursor->info.target_pathname, info.target_pathname))) {
			memcpy(&cursor->info, &info, sizeof(info));
			spin_unlock(&susfs_spin_lock);
			susfs_update_sus_mount_inode(info.target_pathname);
			SUSFS_LOGI("target_pathname: '%s', target_dev: '%lu', is successfully updated to LH_SUS_MOUNT\n",
						info.target_pathname, info.target_dev);
			return 0;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_list = kmalloc(sizeof(struct st_susfs_sus_mount_list), GFP_KERNEL);
	if (!new_list) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

	memcpy(&new_list->info, &info, sizeof(info));
	new_list->info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	susfs_update_sus_mount_inode(new_list->info.target_pathname);

	INIT_LIST_HEAD(&new_list->list);
	spin_lock(&susfs_spin_lock);
	list_add_tail(&new_list->list, &LH_SUS_MOUNT);
	SUSFS_LOGI("target_pathname: '%s', target_dev: '%lu', is successfully added to LH_SUS_MOUNT\n",
				new_list->info.target_pathname, new_list->info.target_dev);
	spin_unlock(&susfs_spin_lock);
	return 0;
}

#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
int susfs_auto_add_sus_bind_mount(const char *pathname, struct path *path_target) {
	struct mount *mnt;
	struct inode *inode;

	mnt = real_mount(path_target->mnt);
	if (mnt->mnt_group_id > 0 && // 0 means no peer group
		mnt->mnt_group_id < DEFAULT_SUS_MNT_GROUP_ID) {
		SUSFS_LOGE("skip setting SUS_MOUNT inode state for path '%s' since its source mount has a legit peer group id\n", pathname);
		// return 0 here as we still want it to be added to try_umount list
		return 0;
	}
	inode = path_target->dentry->d_inode;
	if (!inode) return 1;
	if (!(inode->i_state & INODE_STATE_SUS_MOUNT)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= INODE_STATE_SUS_MOUNT;
		spin_unlock(&inode->i_lock);
		SUSFS_LOGI("set SUS_MOUNT inode state for source bind mount path '%s'\n", pathname);
	}
	return 0;
}
#endif // #ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT

#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
void susfs_auto_add_sus_ksu_default_mount(const char __user *to_pathname) {
	char *pathname = NULL;
	struct path path;
	struct inode *inode;

	pathname = kmalloc(SUSFS_MAX_LEN_PATHNAME, GFP_KERNEL);
	if (!pathname) {
		SUSFS_LOGE("no enough memory\n");
		return;
	}
	// Here we need to re-retrieve the struct path as we want the new struct path, not the old one
	if (strncpy_from_user(pathname, to_pathname, SUSFS_MAX_LEN_PATHNAME - 1) < 0) {
		SUSFS_LOGE("strncpy_from_user()\n");
		goto out_free_pathname;
	}
	pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	if ((!strncmp(pathname, "/data/adb/modules", 17) ||
		 !strncmp(pathname, "/debug_ramdisk", 14) ||
		 !strncmp(pathname, "/system", 7) ||
		 !strncmp(pathname, "/system_ext", 11) ||
		 !strncmp(pathname, "/vendor", 7) ||
		 !strncmp(pathname, "/product", 8) ||
		 !strncmp(pathname, "/odm", 4)) &&
		 !kern_path(pathname, LOOKUP_FOLLOW, &path)) {
		goto set_inode_sus_mount;
	}
	goto out_free_pathname;
set_inode_sus_mount:
	inode = path.dentry->d_inode;
	if (!inode) {
		goto out_path_put;
	}
	if (!(inode->i_state & INODE_STATE_SUS_MOUNT)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= INODE_STATE_SUS_MOUNT;
		spin_unlock(&inode->i_lock);
		SUSFS_LOGI("set SUS_MOUNT inode state for default KSU mount path '%s'\n", pathname);
	}
out_path_put:
	path_put(&path);
out_free_pathname:
	kfree(pathname);
}
#endif // #ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_MOUNT

/* sus_kstat */
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
static DEFINE_HASHTABLE(SUS_KSTAT_HLIST, 10);
static int susfs_update_sus_kstat_inode(char *target_pathname) {
	struct path p;
	struct inode *inode = NULL;
	int err = 0;

	err = kern_path(target_pathname, LOOKUP_FOLLOW, &p);
	if (err) {
		SUSFS_LOGE("Failed opening file '%s'\n", target_pathname);
		return 1;
	}

	// We don't allow path of which filesystem type is "tmpfs", because its inode->i_ino is starting from 1 again,
	// which will cause wrong comparison in function susfs_sus_ino_for_filldir64()
	if (strcmp(p.mnt->mnt_sb->s_type->name, "tmpfs") == 0) {
		SUSFS_LOGE("target_pathname: '%s' cannot be added since its filesystem is 'tmpfs'\n", target_pathname);
		path_put(&p);
		return 1;
	}

	inode = d_inode(p.dentry);
	if (!inode) {
		path_put(&p);
		SUSFS_LOGE("inode is NULL\n");
		return 1;
	}

	if (!(inode->i_state & INODE_STATE_SUS_KSTAT)) {
		spin_lock(&inode->i_lock);
		inode->i_state |= INODE_STATE_SUS_KSTAT;
		spin_unlock(&inode->i_lock);
	}
	path_put(&p);
	return 0;
}

int susfs_add_sus_kstat(struct st_susfs_sus_kstat* __user user_info) {
	struct st_susfs_sus_kstat info;
	struct st_susfs_sus_kstat_hlist *new_entry, *tmp_entry;
	struct hlist_node *tmp_node;
	int bkt;
	bool update_hlist = false;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

	if (strlen(info.target_pathname) == 0) {
		SUSFS_LOGE("target_pathname is an empty string\n");
		return 1;
	}

	spin_lock(&susfs_spin_lock);
	hash_for_each_safe(SUS_KSTAT_HLIST, bkt, tmp_node, tmp_entry, node) {
		if (!strcmp(tmp_entry->info.target_pathname, info.target_pathname)) {
			hash_del_rcu(&tmp_entry->node);
			kfree_rcu(tmp_entry, rcu);
			update_hlist = true;
			break;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_entry = kmalloc(sizeof(struct st_susfs_sus_kstat_hlist), GFP_KERNEL);
	if (!new_entry) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)
#ifdef CONFIG_MIPS
	info.spoofed_dev = new_decode_dev(info.spoofed_dev);
#else
	info.spoofed_dev = huge_decode_dev(info.spoofed_dev);
#endif /* CONFIG_MIPS */
#else
	info.spoofed_dev = old_decode_dev(info.spoofed_dev);
#endif /* defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64) */

	new_entry->target_ino = info.target_ino;
	memcpy(&new_entry->info, &info, sizeof(info));

	if (susfs_update_sus_kstat_inode(new_entry->info.target_pathname)) {
		kfree(new_entry);
		return 1;
	}

	spin_lock(&susfs_spin_lock);
	hash_add_rcu(SUS_KSTAT_HLIST, &new_entry->node, info.target_ino);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	if (update_hlist) {
		SUSFS_LOGI("is_statically: '%d', target_ino: '%lu', target_pathname: '%s', spoofed_ino: '%lu', spoofed_dev: '%lu', spoofed_nlink: '%u', spoofed_size: '%llu', spoofed_atime_tv_sec: '%ld', spoofed_mtime_tv_sec: '%ld', spoofed_ctime_tv_sec: '%ld', spoofed_atime_tv_nsec: '%ld', spoofed_mtime_tv_nsec: '%ld', spoofed_ctime_tv_nsec: '%ld', spoofed_blksize: '%lu', spoofed_blocks: '%llu', is successfully added to SUS_KSTAT_HLIST\n",
				new_entry->info.is_statically, new_entry->info.target_ino, new_entry->info.target_pathname,
				new_entry->info.spoofed_ino, new_entry->info.spoofed_dev,
				new_entry->info.spoofed_nlink, new_entry->info.spoofed_size,
				new_entry->info.spoofed_atime_tv_sec, new_entry->info.spoofed_mtime_tv_sec, new_entry->info.spoofed_ctime_tv_sec,
				new_entry->info.spoofed_atime_tv_nsec, new_entry->info.spoofed_mtime_tv_nsec, new_entry->info.spoofed_ctime_tv_nsec,
				new_entry->info.spoofed_blksize, new_entry->info.spoofed_blocks);
	} else {
		SUSFS_LOGI("is_statically: '%d', target_ino: '%lu', target_pathname: '%s', spoofed_ino: '%lu', spoofed_dev: '%lu', spoofed_nlink: '%u', spoofed_size: '%llu', spoofed_atime_tv_sec: '%ld', spoofed_mtime_tv_sec: '%ld', spoofed_ctime_tv_sec: '%ld', spoofed_atime_tv_nsec: '%ld', spoofed_mtime_tv_nsec: '%ld', spoofed_ctime_tv_nsec: '%ld', spoofed_blksize: '%lu', spoofed_blocks: '%llu', is successfully updated to SUS_KSTAT_HLIST\n",
				new_entry->info.is_statically, new_entry->info.target_ino, new_entry->info.target_pathname,
				new_entry->info.spoofed_ino, new_entry->info.spoofed_dev,
				new_entry->info.spoofed_nlink, new_entry->info.spoofed_size,
				new_entry->info.spoofed_atime_tv_sec, new_entry->info.spoofed_mtime_tv_sec, new_entry->info.spoofed_ctime_tv_sec,
				new_entry->info.spoofed_atime_tv_nsec, new_entry->info.spoofed_mtime_tv_nsec, new_entry->info.spoofed_ctime_tv_nsec,
				new_entry->info.spoofed_blksize, new_entry->info.spoofed_blocks);
	}
#else
	if (update_hlist) {
		SUSFS_LOGI("is_statically: '%d', target_ino: '%lu', target_pathname: '%s', spoofed_ino: '%lu', spoofed_dev: '%lu', spoofed_nlink: '%u', spoofed_size: '%u', spoofed_atime_tv_sec: '%ld', spoofed_mtime_tv_sec: '%ld', spoofed_ctime_tv_sec: '%ld', spoofed_atime_tv_nsec: '%ld', spoofed_mtime_tv_nsec: '%ld', spoofed_ctime_tv_nsec: '%ld', spoofed_blksize: '%lu', spoofed_blocks: '%llu', is successfully added to SUS_KSTAT_HLIST\n",
				new_entry->info.is_statically, new_entry->info.target_ino, new_entry->info.target_pathname,
				new_entry->info.spoofed_ino, new_entry->info.spoofed_dev,
				new_entry->info.spoofed_nlink, new_entry->info.spoofed_size,
				new_entry->info.spoofed_atime_tv_sec, new_entry->info.spoofed_mtime_tv_sec, new_entry->info.spoofed_ctime_tv_sec,
				new_entry->info.spoofed_atime_tv_nsec, new_entry->info.spoofed_mtime_tv_nsec, new_entry->info.spoofed_ctime_tv_nsec,
				new_entry->info.spoofed_blksize, new_entry->info.spoofed_blocks);
	} else {
		SUSFS_LOGI("is_statically: '%d', target_ino: '%lu', target_pathname: '%s', spoofed_ino: '%lu', spoofed_dev: '%lu', spoofed_nlink: '%u', spoofed_size: '%u', spoofed_atime_tv_sec: '%ld', spoofed_mtime_tv_sec: '%ld', spoofed_ctime_tv_sec: '%ld', spoofed_atime_tv_nsec: '%ld', spoofed_mtime_tv_nsec: '%ld', spoofed_ctime_tv_nsec: '%ld', spoofed_blksize: '%lu', spoofed_blocks: '%llu', is successfully updated to SUS_KSTAT_HLIST\n",
				new_entry->info.is_statically, new_entry->info.target_ino, new_entry->info.target_pathname,
				new_entry->info.spoofed_ino, new_entry->info.spoofed_dev,
				new_entry->info.spoofed_nlink, new_entry->info.spoofed_size,
				new_entry->info.spoofed_atime_tv_sec, new_entry->info.spoofed_mtime_tv_sec, new_entry->info.spoofed_ctime_tv_sec,
				new_entry->info.spoofed_atime_tv_nsec, new_entry->info.spoofed_mtime_tv_nsec, new_entry->info.spoofed_ctime_tv_nsec,
				new_entry->info.spoofed_blksize, new_entry->info.spoofed_blocks);
	}
#endif
	spin_unlock(&susfs_spin_lock);
	return 0;
}

int susfs_update_sus_kstat(struct st_susfs_sus_kstat* __user user_info) {
	struct st_susfs_sus_kstat info;
	struct st_susfs_sus_kstat_hlist *new_entry, *tmp_entry;
	struct hlist_node *tmp_node;
	int bkt;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

	new_entry = kmalloc(sizeof(struct st_susfs_sus_kstat_hlist), GFP_KERNEL);
	if (!new_entry) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

	if (susfs_update_sus_kstat_inode(info.target_pathname)) {
		kfree(new_entry);
		return 1;
	}

	spin_lock(&susfs_spin_lock);
	hash_for_each_safe(SUS_KSTAT_HLIST, bkt, tmp_node, tmp_entry, node) {
		if (!strcmp(tmp_entry->info.target_pathname, info.target_pathname)) {
			memcpy(&new_entry->info, &tmp_entry->info, sizeof(tmp_entry->info));
			new_entry->info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
			SUSFS_LOGI("updating target_ino from '%lu' to '%lu' for pathname: '%s' in SUS_KSTAT_HLIST\n",
							new_entry->info.target_ino, info.target_ino, info.target_pathname);
			new_entry->target_ino = info.target_ino;
			new_entry->info.target_ino = info.target_ino;
			if (info.spoofed_size > 0) {
				SUSFS_LOGI("updating spoofed_size from '%lld' to '%lld' for pathname: '%s' in SUS_KSTAT_HLIST\n",
								new_entry->info.spoofed_size, info.spoofed_size, info.target_pathname);
				new_entry->info.spoofed_size = info.spoofed_size;
			}
			if (info.spoofed_blocks > 0) {
				SUSFS_LOGI("updating spoofed_blocks from '%llu' to '%llu' for pathname: '%s' in SUS_KSTAT_HLIST\n",
								new_entry->info.spoofed_blocks, info.spoofed_blocks, info.target_pathname);
				new_entry->info.spoofed_blocks = info.spoofed_blocks;
			}
			hash_del_rcu(&tmp_entry->node);
			kfree_rcu(tmp_entry, rcu);
			hash_add_rcu(SUS_KSTAT_HLIST, &new_entry->node, info.target_ino);
			spin_unlock(&susfs_spin_lock);
			return 0;
		}
	}
	spin_unlock(&susfs_spin_lock);
	kfree(new_entry);
	return 1;
}

void susfs_sus_ino_for_generic_fillattr(unsigned long ino, struct kstat *stat) {
	struct st_susfs_sus_kstat_hlist *entry;

	rcu_read_lock();
	hash_for_each_possible_rcu(SUS_KSTAT_HLIST, entry, node, ino) {
		if (entry->target_ino == ino) {
			stat->dev = entry->info.spoofed_dev;
			stat->ino = entry->info.spoofed_ino;
			stat->nlink = entry->info.spoofed_nlink;
			stat->size = entry->info.spoofed_size;
			stat->atime.tv_sec = entry->info.spoofed_atime_tv_sec;
			stat->atime.tv_nsec = entry->info.spoofed_atime_tv_nsec;
			stat->mtime.tv_sec = entry->info.spoofed_mtime_tv_sec;
			stat->mtime.tv_nsec = entry->info.spoofed_mtime_tv_nsec;
			stat->ctime.tv_sec = entry->info.spoofed_ctime_tv_sec;
			stat->ctime.tv_nsec = entry->info.spoofed_ctime_tv_nsec;
			stat->blocks = entry->info.spoofed_blocks;
			stat->blksize = entry->info.spoofed_blksize;
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
}

void susfs_sus_ino_for_show_map_vma(unsigned long ino, dev_t *out_dev, unsigned long *out_ino) {
	struct st_susfs_sus_kstat_hlist *entry;

	rcu_read_lock();
	hash_for_each_possible_rcu(SUS_KSTAT_HLIST, entry, node, ino) {
		if (entry->target_ino == ino) {
			*out_dev = entry->info.spoofed_dev;
			*out_ino = entry->info.spoofed_ino;
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
}
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_KSTAT

/* try_umount */
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
static LIST_HEAD(LH_TRY_UMOUNT_PATH);
int susfs_add_try_umount(struct st_susfs_try_umount* __user user_info) {
	struct st_susfs_try_umount_list *cursor = NULL, *temp = NULL;
	struct st_susfs_try_umount_list *new_list = NULL;
	struct st_susfs_try_umount info;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

	spin_lock(&susfs_spin_lock);
	list_for_each_entry_safe(cursor, temp, &LH_TRY_UMOUNT_PATH, list) {
		if (unlikely(!strcmp(info.target_pathname, cursor->info.target_pathname))) {
			spin_unlock(&susfs_spin_lock);
			SUSFS_LOGE("target_pathname: '%s' is already created in LH_TRY_UMOUNT_PATH\n", info.target_pathname);
			return 1;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_list = kmalloc(sizeof(struct st_susfs_try_umount_list), GFP_KERNEL);
	if (!new_list) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

	memcpy(&new_list->info, &info, sizeof(info));

	INIT_LIST_HEAD(&new_list->list);
	spin_lock(&susfs_spin_lock);
	list_add_tail(&new_list->list, &LH_TRY_UMOUNT_PATH);
	spin_unlock(&susfs_spin_lock);
	SUSFS_LOGI("target_pathname: '%s', mnt_mode: %d, is successfully added to LH_TRY_UMOUNT_PATH\n", new_list->info.target_pathname, new_list->info.mnt_mode);
	return 0;
}

void susfs_try_umount(uid_t target_uid) {
	struct st_susfs_try_umount_list *cursor = NULL;

	// We should umount in reversed order
	list_for_each_entry_reverse(cursor, &LH_TRY_UMOUNT_PATH, list) {
		if (cursor->info.mnt_mode == TRY_UMOUNT_DEFAULT) {
			ksu_try_umount(cursor->info.target_pathname, false, 0, target_uid);
		} else if (cursor->info.mnt_mode == TRY_UMOUNT_DETACH) {
			ksu_try_umount(cursor->info.target_pathname, false, MNT_DETACH, target_uid);
		} else {
			SUSFS_LOGE("failed umounting '%s' for uid: %d, mnt_mode '%d' not supported\n",
							cursor->info.target_pathname, target_uid, cursor->info.mnt_mode);
		}
	}
}

#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
void susfs_auto_add_try_umount_for_bind_mount(struct path *path) {
	struct st_susfs_try_umount_list *cursor = NULL, *temp = NULL;
	struct st_susfs_try_umount_list *new_list = NULL;
	char *pathname = NULL, *dpath = NULL;
#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
	bool is_magic_mount_path = false;
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
	if (path->dentry->d_inode->i_state & INODE_STATE_SUS_KSTAT) {
		SUSFS_LOGI("skip adding path to try_umount list as its inode is flagged INODE_STATE_SUS_KSTAT already\n");
		return;
	}
#endif

	pathname = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pathname) {
		SUSFS_LOGE("no enough memory\n");
		return;
	}

	dpath = d_path(path, pathname, PAGE_SIZE);
	if (IS_ERR_OR_NULL(dpath)) {
		SUSFS_LOGE("dpath is invalid\n");
		goto out_free_pathname;
	}

#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
	if (strstr(dpath, MAGIC_MOUNT_WORKDIR)) {
		is_magic_mount_path = true;
	}
#endif

	spin_lock(&susfs_spin_lock);
	list_for_each_entry_safe(cursor, temp, &LH_TRY_UMOUNT_PATH, list) {
#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
		if (is_magic_mount_path && strstr(dpath, cursor->info.target_pathname)) {
			spin_unlock(&susfs_spin_lock);
			goto out_free_pathname;
		}
#endif
		if (unlikely(!strcmp(dpath, cursor->info.target_pathname))) {
			spin_unlock(&susfs_spin_lock);
			SUSFS_LOGE("target_pathname: '%s', ino: %lu, is already created in LH_TRY_UMOUNT_PATH\n",
							dpath, path->dentry->d_inode->i_ino);
			goto out_free_pathname;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_list = kmalloc(sizeof(struct st_susfs_try_umount_list), GFP_KERNEL);
	if (!new_list) {
		SUSFS_LOGE("no enough memory\n");
		goto out_free_pathname;
	}

#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
	if (is_magic_mount_path) {
		strncpy(new_list->info.target_pathname, dpath + strlen(MAGIC_MOUNT_WORKDIR), SUSFS_MAX_LEN_PATHNAME - 1);
		goto out_add_to_list;
	}
#endif
	strncpy(new_list->info.target_pathname, dpath, SUSFS_MAX_LEN_PATHNAME - 1);

#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
out_add_to_list:
#endif
	new_list->info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	new_list->info.mnt_mode = TRY_UMOUNT_DETACH;

	INIT_LIST_HEAD(&new_list->list);
	spin_lock(&susfs_spin_lock);
	list_add_tail(&new_list->list, &LH_TRY_UMOUNT_PATH);
	spin_unlock(&susfs_spin_lock);
	SUSFS_LOGI("target_pathname: '%s', ino: %lu, mnt_mode: %d, is successfully added to LH_TRY_UMOUNT_PATH\n",
					new_list->info.target_pathname, path->dentry->d_inode->i_ino, new_list->info.mnt_mode);
out_free_pathname:
	kfree(pathname);
}
#endif // #ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
#endif // #ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT

/* spoof_uname */
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
static DEFINE_SPINLOCK(susfs_uname_spin_lock);
static struct st_susfs_uname my_uname;
static void susfs_my_uname_init(void) {
	memset(&my_uname, 0, sizeof(my_uname));
}

int susfs_set_uname(struct st_susfs_uname* __user user_info) {
	struct st_susfs_uname info;
	unsigned long flags;

	if (copy_from_user(&info, user_info, sizeof(struct st_susfs_uname))) {
		SUSFS_LOGE("failed copying from userspace.\n");
		return 1;
	}
	info.release[__NEW_UTS_LEN] = '\0';
	info.version[__NEW_UTS_LEN] = '\0';

	spin_lock_irqsave(&susfs_uname_spin_lock, flags);
	if (!strcmp(info.release, "default")) {
		strncpy(my_uname.release, utsname()->release, __NEW_UTS_LEN);
	} else {
		strncpy(my_uname.release, info.release, __NEW_UTS_LEN);
	}
	my_uname.release[__NEW_UTS_LEN] = '\0';
	if (!strcmp(info.version, "default")) {
		strncpy(my_uname.version, utsname()->version, __NEW_UTS_LEN);
	} else {
		strncpy(my_uname.version, info.version, __NEW_UTS_LEN);
	}
	my_uname.version[__NEW_UTS_LEN] = '\0';
	spin_unlock_irqrestore(&susfs_uname_spin_lock, flags);
	SUSFS_LOGI("setting spoofed release: '%s', version: '%s'\n",
				my_uname.release, my_uname.version);
	return 0;
}

void susfs_spoof_uname(struct new_utsname* tmp) {
	unsigned long flags;

	spin_lock_irqsave(&susfs_uname_spin_lock, flags);
	if (unlikely(my_uname.release[0] == '\0')) {
		spin_unlock_irqrestore(&susfs_uname_spin_lock, flags);
		return;
	}
	strncpy(tmp->release, my_uname.release, __NEW_UTS_LEN);
	tmp->release[__NEW_UTS_LEN] = '\0';
	strncpy(tmp->version, my_uname.version, __NEW_UTS_LEN);
	tmp->version[__NEW_UTS_LEN] = '\0';
	spin_unlock_irqrestore(&susfs_uname_spin_lock, flags);
}
#endif // #ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME

/* set_log */
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
void susfs_set_log(bool enabled) {
	spin_lock(&susfs_spin_lock);
	susfs_is_log_enabled = enabled;
	spin_unlock(&susfs_spin_lock);
	if (susfs_is_log_enabled) {
		pr_info("susfs: enable logging to kernel");
	} else {
		pr_info("susfs: disable logging to kernel");
	}
}
#endif // #ifdef CONFIG_KSU_SUSFS_ENABLE_LOG

/* spoof_cmdline_or_bootconfig */
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
static char *fake_cmdline_or_bootconfig = NULL;
int susfs_set_cmdline_or_bootconfig(char* __user user_fake_cmdline_or_bootconfig) {
	int res;
	char *tmp_buf;

	if (!fake_cmdline_or_bootconfig) {
		// 4096 is enough I guess
		fake_cmdline_or_bootconfig = kmalloc(SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE, GFP_KERNEL);
		if (!fake_cmdline_or_bootconfig) {
			SUSFS_LOGE("no enough memory\n");
			return -ENOMEM;
		}
	}

	tmp_buf = kzalloc(SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	res = strncpy_from_user(tmp_buf, user_fake_cmdline_or_bootconfig, SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE - 1);
	if (res > 0) {
		tmp_buf[SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE - 1] = '\0';
		spin_lock(&susfs_spin_lock);
		memcpy(fake_cmdline_or_bootconfig, tmp_buf, SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE);
		fake_cmdline_or_bootconfig[SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE - 1] = '\0';
		spin_unlock(&susfs_spin_lock);
	}
	kfree(tmp_buf);

	if (res > 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
		SUSFS_LOGI("fake_cmdline_or_bootconfig is set, length of string: %lu\n", strlen(fake_cmdline_or_bootconfig));
#else
		SUSFS_LOGI("fake_cmdline_or_bootconfig is set, length of string: %u\n", strlen(fake_cmdline_or_bootconfig));
#endif
		return 0;
	}
	SUSFS_LOGI("failed setting fake_cmdline_or_bootconfig\n");
	return res;
}

int susfs_spoof_cmdline_or_bootconfig(struct seq_file *m) {
	if (fake_cmdline_or_bootconfig != NULL) {
		seq_puts(m, fake_cmdline_or_bootconfig);
		return 0;
	}
	return 1;
}
#endif

/* open_redirect */
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
static DEFINE_HASHTABLE(OPEN_REDIRECT_HLIST, 10);
static int susfs_update_open_redirect_inode(struct st_susfs_open_redirect_hlist *new_entry) {
	struct path path_target;
	struct inode *inode_target;
	int err = 0;

	err = kern_path(new_entry->target_pathname, LOOKUP_FOLLOW, &path_target);
	if (err) {
		SUSFS_LOGE("Failed opening file '%s'\n", new_entry->target_pathname);
		return err;
	}

	inode_target = d_inode(path_target.dentry);
	if (!inode_target) {
		SUSFS_LOGE("inode_target is NULL\n");
		err = 1;
		goto out_path_put_target;
	}

	spin_lock(&inode_target->i_lock);
	inode_target->i_state |= INODE_STATE_OPEN_REDIRECT;
	spin_unlock(&inode_target->i_lock);

out_path_put_target:
	path_put(&path_target);
	return err;
}

int susfs_add_open_redirect(struct st_susfs_open_redirect* __user user_info) {
	struct st_susfs_open_redirect info;
	struct st_susfs_open_redirect_hlist *new_entry, *tmp_entry;
	struct hlist_node *tmp_node;
	int bkt;
	bool update_hlist = false;

	if (copy_from_user(&info, user_info, sizeof(info))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}
	info.target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	info.redirected_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';

	spin_lock(&susfs_spin_lock);
	hash_for_each_safe(OPEN_REDIRECT_HLIST, bkt, tmp_node, tmp_entry, node) {
		if (!strcmp(tmp_entry->target_pathname, info.target_pathname)) {
			hash_del_rcu(&tmp_entry->node);
			kfree_rcu(tmp_entry, rcu);
			update_hlist = true;
			break;
		}
	}
	spin_unlock(&susfs_spin_lock);

	new_entry = kmalloc(sizeof(struct st_susfs_open_redirect_hlist), GFP_KERNEL);
	if (!new_entry) {
		SUSFS_LOGE("no enough memory\n");
		return 1;
	}

	new_entry->target_ino = info.target_ino;
	strncpy(new_entry->target_pathname, info.target_pathname, SUSFS_MAX_LEN_PATHNAME - 1);
	new_entry->target_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	strncpy(new_entry->redirected_pathname, info.redirected_pathname, SUSFS_MAX_LEN_PATHNAME - 1);
	new_entry->redirected_pathname[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
	if (susfs_update_open_redirect_inode(new_entry)) {
		SUSFS_LOGE("failed adding path '%s' to OPEN_REDIRECT_HLIST\n", new_entry->target_pathname);
		kfree(new_entry);
		return 1;
	}

	spin_lock(&susfs_spin_lock);
	hash_add_rcu(OPEN_REDIRECT_HLIST, &new_entry->node, info.target_ino);
	if (update_hlist) {
		SUSFS_LOGI("target_ino: '%lu', target_pathname: '%s', redirected_pathname: '%s', is successfully updated to OPEN_REDIRECT_HLIST\n",
				new_entry->target_ino, new_entry->target_pathname, new_entry->redirected_pathname);	
	} else {
		SUSFS_LOGI("target_ino: '%lu', target_pathname: '%s' redirected_pathname: '%s', is successfully added to OPEN_REDIRECT_HLIST\n",
				new_entry->target_ino, new_entry->target_pathname, new_entry->redirected_pathname);
	}
	spin_unlock(&susfs_spin_lock);
	return 0;
}

struct filename* susfs_get_redirected_path(unsigned long ino) {
	struct st_susfs_open_redirect_hlist *entry;
	char redirected_pathname[SUSFS_MAX_LEN_PATHNAME];
	bool found = false;

	rcu_read_lock();
	hash_for_each_possible_rcu(OPEN_REDIRECT_HLIST, entry, node, ino) {
		if (entry->target_ino == ino) {
			SUSFS_LOGI("Redirect for ino: %lu\n", ino);
			strscpy(redirected_pathname, entry->redirected_pathname, SUSFS_MAX_LEN_PATHNAME);
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (found)
		return getname_kernel(redirected_pathname);
	return ERR_PTR(-ENOENT);
}
#endif // #ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT

/* sus_su */
#ifdef CONFIG_KSU_SUSFS_SUS_SU
bool susfs_is_sus_su_hooks_enabled __read_mostly = false;
static int susfs_sus_su_working_mode = 0;
extern void ksu_susfs_enable_sus_su(void);
extern void ksu_susfs_disable_sus_su(void);

int susfs_get_sus_su_working_mode(void) {
	return susfs_sus_su_working_mode;
}

int susfs_sus_su(struct st_sus_su* __user user_info) {
	struct st_sus_su info;
	int last_working_mode = susfs_sus_su_working_mode;

	if (copy_from_user(&info, user_info, sizeof(struct st_sus_su))) {
		SUSFS_LOGE("failed copying from userspace\n");
		return 1;
	}

	if (info.mode == SUS_SU_WITH_HOOKS) {
		if (last_working_mode == SUS_SU_WITH_HOOKS) {
			SUSFS_LOGE("current sus_su mode is already %d\n", SUS_SU_WITH_HOOKS);
			return 1;
		}
		if (last_working_mode != SUS_SU_DISABLED) {
			SUSFS_LOGE("please make sure the current sus_su mode is %d first\n", SUS_SU_DISABLED);
			return 2;
		}
		ksu_susfs_enable_sus_su();
		susfs_sus_su_working_mode = SUS_SU_WITH_HOOKS;
		susfs_is_sus_su_hooks_enabled = true;
		SUSFS_LOGI("core kprobe hooks for ksu are disabled!\n");
		SUSFS_LOGI("non-kprobe hook sus_su is enabled!\n");
		SUSFS_LOGI("sus_su mode: %d\n", SUS_SU_WITH_HOOKS);
		return 0;
	} else if (info.mode == SUS_SU_DISABLED) {
		if (last_working_mode == SUS_SU_DISABLED) {
			SUSFS_LOGE("current sus_su mode is already %d\n", SUS_SU_DISABLED);
			return 1;
		}
		susfs_is_sus_su_hooks_enabled = false;
		ksu_susfs_disable_sus_su();
		susfs_sus_su_working_mode = SUS_SU_DISABLED;
		if (last_working_mode == SUS_SU_WITH_HOOKS) {
			SUSFS_LOGI("core kprobe hooks for ksu are enabled!\n");
			goto out;
		}
out:
		if (copy_to_user(user_info, &info, sizeof(info)))
			SUSFS_LOGE("copy_to_user() failed\n");
		return 0;
	} else if (info.mode == SUS_SU_WITH_OVERLAY) {
		SUSFS_LOGE("sus_su mode %d is deprecated\n", SUS_SU_WITH_OVERLAY);
		return 1;
	}
	return 1;
}
#endif // #ifdef CONFIG_KSU_SUSFS_SUS_SU

/* susfs_init */
void susfs_init(void) {
	static bool susfs_initialized = false;

	if (susfs_initialized)
		return;
	susfs_initialized = true;

	spin_lock_init(&susfs_spin_lock);
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
	spin_lock_init(&susfs_uname_spin_lock);
	susfs_my_uname_init();
#endif
	SUSFS_LOGI("susfs is initialized! version: " SUSFS_VERSION " \n");
}

#ifndef ksu_access_ok
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define ksu_access_ok(addr, size) access_ok(addr, size)
#else
#define ksu_access_ok(addr, size) access_ok(VERIFY_READ, addr, size)
#endif
#endif

/* prctl dispatcher for userspace ksu_susfs tool */
int susfs_handle_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
	if (current_uid().val != 0)
		return -EINVAL;

	if (option != (int)0xDEADBEEF)
		return -EINVAL;

	/* SHOW_VERSION, SHOW_ENABLED_FEATURES and SHOW_VARIANT can be read by userspace */
	if (arg2 == CMD_SUSFS_SHOW_VERSION) {
		int error = 0;
		int len_of_susfs_version = strlen(SUSFS_VERSION);
		char *susfs_version = SUSFS_VERSION;
		if (!ksu_access_ok((void __user*)arg3, len_of_susfs_version+1)) {
			pr_err("susfs: CMD_SUSFS_SHOW_VERSION -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SHOW_VERSION -> arg5 is not accessible\n");
			return 0;
		}
		error = copy_to_user((void __user*)arg3, (void*)susfs_version, len_of_susfs_version+1);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_SHOW_ENABLED_FEATURES) {
		int error = 0;
		u64 enabled_features = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(u64))) {
			pr_err("susfs: CMD_SUSFS_SHOW_ENABLED_FEATURES -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SHOW_ENABLED_FEATURES -> arg5 is not accessible\n");
			return 0;
		}
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
		enabled_features |= (1ULL << 0);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
		enabled_features |= (1ULL << 1);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
		enabled_features |= (1ULL << 2);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
		enabled_features |= (1ULL << 3);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
		enabled_features |= (1ULL << 4);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_OVERLAYFS
		enabled_features |= (1ULL << 5);
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
		enabled_features |= (1ULL << 6);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
		enabled_features |= (1ULL << 7);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
		enabled_features |= (1ULL << 8);
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
		enabled_features |= (1ULL << 9);
#endif
#ifdef CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
		enabled_features |= (1ULL << 10);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
		enabled_features |= (1ULL << 11);
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
		enabled_features |= (1ULL << 12);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_SU
		enabled_features |= (1ULL << 13);
#endif
		error = copy_to_user((void __user*)arg3, (void*)&enabled_features, sizeof(enabled_features));
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_SHOW_VARIANT) {
		int error = 0;
		size_t len_of_susfs_variant = strlen(SUSFS_VARIANT);
		char *susfs_variant = SUSFS_VARIANT;
		if (!ksu_access_ok((void __user*)arg3, len_of_susfs_variant+1)) {
			pr_err("susfs: CMD_SUSFS_SHOW_VARIANT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SHOW_VARIANT -> arg5 is not accessible\n");
			return 0;
		}
		error = copy_to_user((void __user*)arg3, (void*)susfs_variant, len_of_susfs_variant+1);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}


#ifdef CONFIG_KSU_SUSFS_SUS_PATH
	if (arg2 == CMD_SUSFS_ADD_SUS_PATH) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_sus_path))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_PATH -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_PATH -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_sus_path((struct st_susfs_sus_path __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
	if (arg2 == CMD_SUSFS_ADD_SUS_MOUNT) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_sus_mount))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_MOUNT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_MOUNT -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_sus_mount((struct st_susfs_sus_mount __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
	if (arg2 == CMD_SUSFS_ADD_SUS_KSTAT) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_sus_kstat))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_KSTAT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_KSTAT -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_sus_kstat((struct st_susfs_sus_kstat __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_UPDATE_SUS_KSTAT) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_sus_kstat))) {
			pr_err("susfs: CMD_SUSFS_UPDATE_SUS_KSTAT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_UPDATE_SUS_KSTAT -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_update_sus_kstat((struct st_susfs_sus_kstat __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_sus_kstat))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_sus_kstat((struct st_susfs_sus_kstat __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
	if (arg2 == CMD_SUSFS_ADD_TRY_UMOUNT) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_try_umount))) {
			pr_err("susfs: CMD_SUSFS_ADD_TRY_UMOUNT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_TRY_UMOUNT -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_try_umount((struct st_susfs_try_umount __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS) {
		susfs_run_try_umount_for_current_mnt_ns();
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
	if (arg2 == CMD_SUSFS_SET_UNAME) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_uname))) {
			pr_err("susfs: CMD_SUSFS_SET_UNAME -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SET_UNAME -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_set_uname((struct st_susfs_uname __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
	if (arg2 == CMD_SUSFS_ENABLE_LOG) {
		int error = 0;
		if (arg3 != 0 && arg3 != 1) {
			pr_err("susfs: CMD_SUSFS_ENABLE_LOG -> arg3 can only be 0 or 1\n");
			return 0;
		}
		susfs_set_log(arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	if (arg2 == CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE)) {
			pr_err("susfs: CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_set_cmdline_or_bootconfig((char __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
	if (arg2 == CMD_SUSFS_ADD_OPEN_REDIRECT) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_susfs_open_redirect))) {
			pr_err("susfs: CMD_SUSFS_ADD_OPEN_REDIRECT -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_ADD_OPEN_REDIRECT -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_add_open_redirect((struct st_susfs_open_redirect __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_SU
	if (arg2 == CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE) {
		int error = 0;
		int last_working_mode = susfs_get_sus_su_working_mode();
		if (!ksu_access_ok((void __user*)arg3, sizeof(last_working_mode))) {
			pr_err("susfs: CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE -> arg5 is not accessible\n");
			return 0;
		}
		error = copy_to_user((void __user*)arg3, &last_working_mode, sizeof(last_working_mode));
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_IS_SUS_SU_READY) {
		int error = 0;
		bool is_sus_su_ready = false;
		if (!ksu_access_ok((void __user*)arg3, sizeof(is_sus_su_ready))) {
			pr_err("susfs: CMD_SUSFS_IS_SUS_SU_READY -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_IS_SUS_SU_READY -> arg5 is not accessible\n");
			return 0;
		}
		error = copy_to_user((void __user*)arg3, &is_sus_su_ready, sizeof(is_sus_su_ready));
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
	if (arg2 == CMD_SUSFS_SUS_SU) {
		int error = 0;
		if (!ksu_access_ok((void __user*)arg3, sizeof(struct st_sus_su))) {
			pr_err("susfs: CMD_SUSFS_SUS_SU -> arg3 is not accessible\n");
			return 0;
		}
		if (!ksu_access_ok((void __user*)arg5, sizeof(error))) {
			pr_err("susfs: CMD_SUSFS_SUS_SU -> arg5 is not accessible\n");
			return 0;
		}
		error = susfs_sus_su((struct st_sus_su __user*)arg3);
		if (copy_to_user((void __user*)arg5, &error, sizeof(error)))
			pr_info("susfs: copy_to_user() failed\n");
		return 0;
	}
#endif

	return -EINVAL;
}

/* sys_reboot dispatcher for userspace ksu_susfs supercall */
int susfs_handle_reboot(unsigned int cmd, void __user *arg) {
	if (current_uid().val != 0)
		return -EPERM;
	if (cmd == CMD_SUSFS_SHOW_VERSION) {
		char ver[16] = {0};
		int err = 0;
		if (!ksu_access_ok(arg, sizeof(ver))) {
			pr_err("susfs: CMD_SUSFS_SHOW_VERSION -> arg is not accessible\n");
			return -EFAULT;
		}
		strscpy(ver, SUSFS_VERSION, sizeof(ver));
		if (copy_to_user(arg, ver, sizeof(ver))) {
			pr_info("susfs: copy_to_user() failed\n");
			return -EFAULT;
		}
		copy_to_user((char __user*)arg + sizeof(ver), &err, sizeof(err));
		return 0;
	}

	if (cmd == CMD_SUSFS_SHOW_VARIANT) {
		char var[16] = {0};
		int err = 0;
		if (!ksu_access_ok(arg, sizeof(var))) {
			pr_err("susfs: CMD_SUSFS_SHOW_VARIANT -> arg is not accessible\n");
			return -EFAULT;
		}
		strscpy(var, SUSFS_VARIANT, sizeof(var));
		if (copy_to_user(arg, var, sizeof(var))) {
			pr_info("susfs: copy_to_user() failed\n");
			return -EFAULT;
		}
		copy_to_user((char __user*)arg + sizeof(var), &err, sizeof(err));
		return 0;
	}

	if (cmd == CMD_SUSFS_SHOW_ENABLED_FEATURES) {
		int user_err = 0;
		u64 enabled_features = 0;
		/* Check if userspace passed struct st_susfs_enabled_features (where err == 126 at offset 8192) */
		if (!copy_from_user(&user_err, (char __user *)arg + 8192, sizeof(int)) && user_err == 126) {

			char *kbuf = kzalloc(8192, GFP_KERNEL);
			if (kbuf) {
				char *p = kbuf;
				size_t rem = 8192;
				int n;
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SUS_PATH\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SUS_MOUNT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SUS_KSTAT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_OVERLAYFS
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SUS_OVERLAYFS\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_TRY_UMOUNT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SPOOF_UNAME\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_ENABLE_LOG\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_OPEN_REDIRECT\n"); p += n; rem -= n;
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_SU
				n = scnprintf(p, rem, "CONFIG_KSU_SUSFS_SUS_SU\n"); p += n; rem -= n;
#endif
				user_err = 0;
				copy_to_user(arg, kbuf, 8192);
				copy_to_user((char __user *)arg + 8192, &user_err, sizeof(int));
				kfree(kbuf);
				return 0;
			}
		}
		/* fallback: bitmask */
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
		enabled_features |= (1ULL << 0);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
		enabled_features |= (1ULL << 1);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
		enabled_features |= (1ULL << 2);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
		enabled_features |= (1ULL << 3);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
		enabled_features |= (1ULL << 4);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_OVERLAYFS
		enabled_features |= (1ULL << 5);
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
		enabled_features |= (1ULL << 6);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
		enabled_features |= (1ULL << 7);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
		enabled_features |= (1ULL << 8);
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
		enabled_features |= (1ULL << 9);
#endif
#ifdef CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
		enabled_features |= (1ULL << 10);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
		enabled_features |= (1ULL << 11);
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
		enabled_features |= (1ULL << 12);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_SU
		enabled_features |= (1ULL << 13);
#endif
		if (copy_to_user(arg, &enabled_features, sizeof(enabled_features)))
			return -EFAULT;
		return 0;
	}

#ifdef CONFIG_KSU_SUSFS_SUS_PATH
	if (cmd == CMD_SUSFS_ADD_SUS_PATH || cmd == CMD_SUSFS_ADD_SUS_PATH_LOOP) {
		char first_byte = 0;
		int err = 0;
		if (copy_from_user(&first_byte, arg, 1))
			return -EFAULT;
		if (first_byte == '/') {
			/* GKI struct: char target_pathname[256]; int err; */
			char pathbuf[SUSFS_MAX_LEN_PATHNAME] = {0};
			struct path p;
			if (strncpy_from_user(pathbuf, (char __user*)arg, SUSFS_MAX_LEN_PATHNAME - 1) < 0)
				return -EFAULT;
			pathbuf[SUSFS_MAX_LEN_PATHNAME - 1] = '\0';
			err = kern_path(pathbuf, LOOKUP_FOLLOW, &p);
			if (!err) {
				unsigned long ino = d_inode(p.dentry)->i_ino;
				path_put(&p);
				err = susfs_add_sus_path_internal(ino, pathbuf);
			} else {
				err = 1;
			}
			copy_to_user((char __user*)arg + SUSFS_MAX_LEN_PATHNAME, &err, sizeof(err));
			return err ? -EINVAL : 0;
		} else {
			/* Non-GKI struct: unsigned long target_ino; char target_pathname[256]; */
			err = susfs_add_sus_path((struct st_susfs_sus_path __user*)arg);
			return err ? -EINVAL : 0;
		}
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
	if (cmd == CMD_SUSFS_ADD_SUS_MOUNT) {
		int err = susfs_add_sus_mount((struct st_susfs_sus_mount __user*)arg);
		return err ? -EINVAL : 0;
	}
	if (cmd == CMD_SUSFS_HIDE_SUS_MNTS_FOR_NON_SU_PROCS) {
		int err = 0;
		copy_to_user((char __user*)arg + sizeof(bool), &err, sizeof(err));
		return 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
	if (cmd == CMD_SUSFS_ADD_SUS_KSTAT) {
		int err = susfs_add_sus_kstat((struct st_susfs_sus_kstat __user*)arg);
		return err ? -EINVAL : 0;
	}
	if (cmd == CMD_SUSFS_UPDATE_SUS_KSTAT) {
		int err = susfs_update_sus_kstat((struct st_susfs_sus_kstat __user*)arg);
		return err ? -EINVAL : 0;
	}
	if (cmd == CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY) {
		int err = susfs_add_sus_kstat((struct st_susfs_sus_kstat __user*)arg);
		return err ? -EINVAL : 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
	if (cmd == CMD_SUSFS_ADD_TRY_UMOUNT) {
		int err = susfs_add_try_umount((struct st_susfs_try_umount __user*)arg);
		return err ? -EINVAL : 0;
	}
	if (cmd == CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS) {
		susfs_run_try_umount_for_current_mnt_ns();
		return 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
	if (cmd == CMD_SUSFS_SET_UNAME) {
		int err = susfs_set_uname((struct st_susfs_uname __user*)arg);
		return err ? -EINVAL : 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
	if (cmd == CMD_SUSFS_ENABLE_LOG) {
		int err = 0;
		bool enabled = false;
		if ((unsigned long)arg <= 1) {
			susfs_set_log((bool)(unsigned long)arg);
			return 0;
		}
		if (!copy_from_user(&enabled, arg, sizeof(bool))) {

			susfs_set_log(enabled);
			copy_to_user((char __user*)arg + sizeof(bool), &err, sizeof(err));
		}
		return 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	if (cmd == CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG) {
		int err = susfs_set_cmdline_or_bootconfig((char __user*)arg);
		return err ? -EINVAL : 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
	if (cmd == CMD_SUSFS_ADD_OPEN_REDIRECT) {
		int err = susfs_add_open_redirect((struct st_susfs_open_redirect __user*)arg);
		return err ? -EINVAL : 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_SU
	if (cmd == CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE) {
		int last_working_mode = susfs_get_sus_su_working_mode();
		if (copy_to_user(arg, &last_working_mode, sizeof(last_working_mode)))
			return -EFAULT;
		return 0;
	}
	if (cmd == CMD_SUSFS_IS_SUS_SU_READY) {
		bool is_sus_su_ready = false;
		if (copy_to_user(arg, &is_sus_su_ready, sizeof(is_sus_su_ready)))
			return -EFAULT;
		return 0;
	}
	if (cmd == CMD_SUSFS_SUS_SU) {
		int err = susfs_sus_su((struct st_sus_su __user*)arg);
		return err ? -EINVAL : 0;
	}
#endif

	return -EINVAL;
}

/* No module exit is needed becuase it should never be a loadable kernel module */
//void __init susfs_exit(void)

