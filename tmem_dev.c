#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <tmem/tmem_ops.h> 

struct tmem_dev {
	void *buf;
	u32 flags;
};


/* 
 * This can be removed, if we assign "namespaces" to each 
 * process opening it
 */
DEFINE_SEMAPHORE(lock); 

int tmem_chrdev_open(struct inode *inode, struct file *filp)
{
	void *buffer = NULL;
	struct tmem_dev *tmem_dev = NULL;
	int ret;

	tmem_dev = kmalloc(sizeof(struct tmem_dev), GFP_KERNEL);
	if (!tmem_dev)
		ret = -ENOMEM;
		
	if (down_trylock(&lock)) {
		ret = -EBUSY;
		goto open_out;
	}

	buffer = kmalloc(TMEM_MAX, GFP_KERNEL);
	if (!buffer) {
		up(&lock);
		ret = -ENOMEM;
		goto open_out;
	}

	tmem_dev->buf = buffer;
	tmem_dev->flags = 0x00000000;
	filp->private_data = tmem_dev;

	return 0;

open_out:
	if (!tmem_dev)
		kfree(tmem_dev);
	return ret;
}

int tmem_chrdev_release(struct inode *inode, struct file *filp)
{
	struct tmem_dev *tmem_dev = (struct tmem_dev *) filp->private_data;

	kfree(tmem_dev->buf);
	kfree(tmem_dev);
	up(&lock);

	return 0;
}

int get_key(void **local_key, __user void *user_key, size_t key_len)
{
	void *key;

	key = kmalloc(max(key_len, sizeof(long)), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	/* The key is a long (for now), so expand it to that size */
	if (copy_from_user(key, user_key, key_len)) {
		kfree(key); 
		return -ERESTARTSYS;
	}

	/* The key needs to be the same every time, so zero out any garbage after it */
	if (sizeof(long) > key_len)
		memset(key + key_len, 0, sizeof(long) - key_len);

	*local_key = key;
	return 0;
}

long tmem_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void *key, *value;
	size_t key_len, value_len;
	struct tmem_dev *tmem_dev;
	struct tmem_request tmem_request;
	int ret;
	
	tmem_dev = (struct tmem_dev *) filp->private_data;
	value = tmem_dev->buf;

	switch (cmd) {
	case TMEM_GET:	
		pr_debug("got into get");
	
		if (copy_from_user(&tmem_request.get, (struct tmem_get_request *) arg, sizeof(struct tmem_get_request))) 
			return -ERESTARTSYS;

		key_len = tmem_request.get.key_len;
		ret = get_key(&key, tmem_request.get.key, key_len);
		if (ret < 0) 
			return ret;


		ret = tmem_get(key, key_len, value, &value_len); 
		if (ret < 0 && ret != -EINVAL)
			break;			
	
		if (ret == -EINVAL) 
			break;
		

		if (copy_to_user(tmem_request.get.value_lenp, &value_len, sizeof(value_len))) {
			ret = -EINVAL;
			break;
		}


		if (copy_to_user(tmem_request.get.value, value, value_len)) 
			ret = -EINVAL;


		break;

	case TMEM_PUT:
		pr_debug("got into put");

		if (copy_from_user(&tmem_request.put, (struct tmem_put_request *) arg, sizeof(struct tmem_put_request))) 
			return -ERESTARTSYS;

		key_len = tmem_request.put.key_len;
		ret = get_key(&key, tmem_request.put.key, key_len);
		if (ret < 0) 
			break;


		value_len = tmem_request.put.value_len;
		if (copy_from_user(value, tmem_request.put.value, value_len)) {
			pr_debug("PUT: copying value to user failed");
			ret = -EINVAL;
			break;
		}

		if (tmem_put(key, key_len, value, value_len) < 0) {
			pr_debug("TMEM_PUT command failed");
			ret = -EINVAL;
		}

		break;

	case TMEM_INVAL:
		pr_debug("Got into invalidate");

		if (copy_from_user(&tmem_request.inval, (struct tmem_invalidate_request *) arg, sizeof(struct tmem_invalidate_request))) 
			return -ERESTARTSYS;

		key_len = tmem_request.inval.key_len;
		ret = get_key(&key, tmem_request.inval.key, key_len);
		if (ret < 0) 
			return ret;
		
		tmem_invalidate(key, key_len);

		break;


	default:

		pr_err("illegal argument");
		return -EINVAL;
	}

	kfree(key);

	return 0;
}

const struct file_operations tmem_fops = {
	.owner = THIS_MODULE,
	.open = tmem_chrdev_open,
	.release = tmem_chrdev_release,
	.unlocked_ioctl = tmem_chrdev_ioctl,
};


struct miscdevice tmem_chrdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tmem_dev",
	.fops = &tmem_fops,
};

static int __init init_func(void)
{
	int ret = 0;

	pr_err("IOCTL Numbers for get, put, invalidate: %ld %ld %d\n",
		TMEM_GET, TMEM_PUT, TMEM_INVAL);

	ret = misc_register(&tmem_chrdev);
	if (ret)
		pr_err("Device registration failed\n");

	return ret;
}


static void __exit exit_func(void)
{
	misc_deregister(&tmem_chrdev);
}


module_init(init_func);
module_exit(exit_func);

MODULE_LICENSE("GPL");

