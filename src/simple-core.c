#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/backing-dev.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/mm.h>

static int simplefs_create (struct inode *dir, struct dentry *dentry, umode_t mode, struct nameidata *data);
static int simplefs_symlink (struct inode * dir, struct dentry *dentry, const char * symname);
static int simplefs_mkdir (struct inode * dir, struct dentry * dentry, umode_t mode);
static int simplefs_mknod (struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev);
static int set_page_dirty_no_writeback(struct page *page);

static int inode_number = 0;

static struct backing_dev_info simplefs_backing_dev_info = {

	.name 			= "simplefs",
	.ra_pages		= 0,	/*	no readahead	*/
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK | /*	no writeback, no dirty page account*/
					  BDI_CAP_MAP_DIRECT |	/*	support direct map */
					  BDI_CAP_MAP_COPY |	/*	support copy map */
					  BDI_CAP_READ_MAP | 	/*	support map read */
					  BDI_CAP_WRITE_MAP | 	/*	support map write */
					  BDI_CAP_EXEC_MAP 		/*	support map execute code */
};

static struct address_space_operations simplefs_aops = {
	.readpage 		=	simple_readpage,
	.write_begin 	=	simple_write_begin,
	.write_end 		=	simple_write_end,
	.set_page_dirty = 	set_page_dirty_no_writeback,
};

static struct file_operations simplefs_file_operations = {
	.read 			=	do_sync_read,
	.aio_read 		= 	generic_file_aio_read,
	.write 			= 	do_sync_write,
	.aio_write		=	generic_file_aio_write,
	.mmap 			=	generic_file_mmap,
	.fsync 			=	noop_fsync,
	.splice_read	=	generic_file_splice_read,
	.splice_write 	=	generic_file_splice_write,
	.llseek 		= 	generic_file_llseek,
};

static struct inode_operations simplefs_file_inode_operations = {
	.setattr 		=	simple_setattr,
	.getattr 		=	simple_getattr,
};

static struct inode_operations simplefs_dir_inode_operations = {
	.create 		=	simplefs_create,
	.lookup 		=	simple_lookup,
	.link 			=	simple_link,
	.unlink 		=	simple_unlink,
	.symlink 		=	simplefs_symlink,
	.mkdir 			=	simplefs_mkdir,
	.rmdir 			=	simple_rmdir,
	.mknod 			=	simplefs_mknod,
	.rename 		=	simple_rename,
};


static struct super_operations simplefs_ops = {
	.statfs 		=	simple_statfs,
	.drop_inode 	= 	generic_delete_inode,
	.show_options 	=	generic_show_options,
};


static int set_page_dirty_no_writeback(struct page *page)  
{  
    if (!PageDirty(page))  
        return !TestSetPageDirty(page);  
    return 0;  
}  

static struct inode *simplefs_get_inode (struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode)
	{
		inode->i_ino 	= ++inode_number;
		/*	init uid, gid, mode for new inode according to posix standards */
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &simplefs_aops;
		inode->i_mapping->backing_dev_info = &simplefs_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT)
		{
			case S_IFREG:
				inode->i_op  = &simplefs_file_inode_operations;
				inode->i_fop = &simplefs_file_operations;
				break;
			case S_IFDIR:
				inode->i_op  = &simplefs_dir_inode_operations;
				inode->i_fop = &simple_dir_operations;
				/*	directory inodes start off with i_nlink == 2 (for "." entry) */
				inc_nlink(inode);
				break;
			case S_IFLNK:
				inode->i_op  = &page_symlink_inode_operations;
				break;
			default:
				init_special_inode(inode, mode, dev);
				break;
		}	
	}

	return inode;
}

static int simplefs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = simplefs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}


static int simplefs_create(struct inode *dir, struct dentry *dentry, umode_t mode, struct nameidata *nd)
{
	return simplefs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int simplefs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = simplefs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}

static int simplefs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = simplefs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}


static int simplefs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *inode;

	sb->s_maxbytes 			= MAX_LFS_FILESIZE;
	sb->s_blocksize			= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic 			= 0xabcdef;
	sb->s_op 				= &simplefs_ops;
	sb->s_time_gran 		= 1;

	inode = simplefs_get_inode(sb, NULL, S_IFDIR, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}


static struct dentry *simplefs_mount (struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, simplefs_fill_super);
}



static struct file_system_type simplefs_type = {
	.name 		= "simplefs",
	.mount 		= simplefs_mount,
	.kill_sb	= kill_litter_super,
};



static int init_simplefs(void)
{
	int err;

	err = bdi_init(&simplefs_backing_dev_info);
	if (err)
		return err;
	err = register_filesystem(&simplefs_type);
	if (err)
		bdi_destroy(&simplefs_backing_dev_info);
	return err;
}


static void exit_simplefs(void)
{
	unregister_filesystem(&simplefs_type);
	bdi_destroy(&simplefs_backing_dev_info);
	return;
}

module_init(init_simplefs);
module_exit(exit_simplefs);
MODULE_LICENSE("GPL");