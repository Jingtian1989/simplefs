#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/dcache.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <asm/uaccess.h>


struct file_contents {
	struct list_head list;
	struct inode *inode;
	void *conts;
};

static int inode_number = 0;
static LIST_HEAD(contents_list);


static struct file_contents *simplefs_find_file(struct inode *inode)
{
	struct file_contents *file_conts;
	list_for_each_entry(file_conts, &contents_list, list)
	{
		if (file_conts->inode == inode)
			return file_conts;
	}
	return NULL;
}

ssize_t simplefs_read (struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	struct file_contents *file_conts;
	struct inode *inode file->f_path.dentry->d_inode;
	unsigned int size = count;

	file_conts = simplefs_find_file(inode);
	if (file_conts == NULL)
		return -EIO;

	if (file_conts->inode->i_size < count)
		size = file_conts->inode->i_size;

	if ((*pos + size) >= file_conts->inode->i_size)
		size = file_conts->inode->i_size - *pos;

	if (copy_to_user(buf, file_conts->conts + *pos, size))
		return -EFAULT;

	return size;
}

ssize_t simplefs_write (struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct file_contents *file_conts;
	struct inode *inode = file->f_path.dentry->d_inode;

	file_conts = simplefs_find_file(inode);
	if (file_conts == NULL)
		return -ENOENT;

	if (copy_from_user(file_conts->conts + *pos, buf, count))
		return -EFAULT;

	return count;
}

static int simplefs_open (struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations simplefs_file_operations = {
	.read  = simplefs_read,
	.write = simplefs_write,
	.open  = simplefs_open, 
};
static const struct inode_operations simplefs_file_inode_operations = {
	.getattr 	=	simple_getattr,
};

static int simple_fs_create (struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;
	struct file_contents *file;
	struct page *page;

	inode = new_inode(dir->i_sb);
	inode->i_blocks = 0;
	inode->i_ino 	= ++inode_number;
	switch (mode & S_IFMT)
	{

		case S_IFDIR:
			inode->i_mode = mode;
			inode->i_op   = &simplefs_dir_inode_operations;
			inode->f_fop  = &simple_dir_operations;
			/*	directorios term nlink=2*/
			inc_nlink(inode);	
			break;
		default:
			inode->i_mode = mode | S_IFREG;
			file = kmalloc(sizeof(*file), GFP_KERNEL);
			if (!file)
				return -EAGAIN;
			inode->i_blocks = 0;
			inode->i_op 	= &simplefs_file_inode_operations;
			inode->i_fop 	= &simplefs_file_operations;
			file->inode = inode;
			page = alloc_page(GFP_KERNEL);
			if (!page)
				goto err_alloc_page;
			file->conts = page_address(page);
			INIT_LIST_HEAD(&file->list);
			list_add_tail(&file->list, &contents_list);
			break;
	}
	/*	fill in inode information for a dentry */
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;

err_alloc_page:
	iput(inode);
	kfree(file);
	return -EINVAL;
}

static int simple_fs_mkdir (struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int res;
	/*	only keep IRWXUGO and ISVTX for directory */
	mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
	res = simple_fs_create(dir, dentry, mode, 0);
	if (!res)
	{
		fsnotify_mkdir(dir, dentry);
	}
	return res;
}

static const struct inode_operations simplefs_dir_inode_operations = {
	.create 	=	simple_fs_create,
	.lookup 	= 	simple_lookup,
	.mkdir 		= 	simple_fs_mkdir,
	.rename 	= 	simple_rename,
	.rmdir 		= 	simple_rmdir,
};

static int simple_fill_super (struct super_block *sb, void *data, int silent)
{

	struct inode  *inode;
	struct dentry *root;

	sb->s_magic 	= 0xabcdef;
	sb->s_op 		= &simplefs_ops;
	/* granularity of c/m/atime in ns. */
	sb->s_time_gran = 1;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;

	inode->i_ino = ++inode_number;
	/*	data block counts */
	inode->i_blocks = 0;
	inode->i_mode 	= S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op   	= &simplefs_dir_inode_operations;
	inode->f_op 	= &simple_dir_operations;
	root = d_make_root(inode);
	if (!root)
	{
		iput(inode);
		return -ENOMEM;
	}	 
	sb->s_root = root;
	return 0;
}


static struct dentry *simple_get_sb (struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, simple_fill_super);
}

static struct file_system_type simple_fs_type = {
	.owner 		= THIS_MODULE,
	.name  		= "simplefs",
	.mount 		= simple_get_sb,
	.kill_sb	= kill_litter_super,
};


static int __init init_simple_fs(void)
{
	INIT_LIST_HEAD(&contents_list);
	return register_filesystem(&simple_fs);
}

static void __exit exit_simple_fs(void)
{
	struct file_contents *file_conts;
	list_for_each_entry(file_conts, &contents_list, list)
	{
		free_page((unsigned int) f->conts);
		kfree(file_conts);
	}
	unregister_filesystem(&simple_fs_type);
}


module_init(init_simple_fs);
module_exit(exit_simple_fs);
MODULE_LICENSE("GPL");