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
#include <linux/delay.h>

#include <tmem/tmem_ops.h> 

struct tmem_dev {
	void *buf;
	long flags;
	size_t generated_size;
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

	buffer = kmalloc(TMEM_MAX, GFP_KERNEL);
	if (!buffer) {
		up(&lock);
		ret = -ENOMEM;
		goto open_out;
	}

	tmem_dev->buf = buffer;
	tmem_dev->flags = 0x00000000;
	tmem_dev->generated_size = 0;
	filp->private_data = tmem_dev;

	return 0;

open_out:
	if (tmem_dev)
		kfree(tmem_dev);

	return ret;
}

int tmem_chrdev_release(struct inode *inode, struct file *filp)
{

	/*
	 * We do not release the device's resources because 
	 * it's now a singleton;
	 */

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


int tmem_chrdev_put(struct tmem_dev *tmem_dev, struct tmem_put_request put_request, long flags) {

	void *key, *value;
	size_t key_len, value_len;
	int ret = 0;

	key_len = put_request.key_len;
	ret = get_key(&key, put_request.key, key_len);
	if (ret < 0) 
		return ret;


	value = tmem_dev->buf;
	value_len = put_request.value_len;

	/* The buffer can only hold so much data */
	if (value_len > TMEM_MAX) {
		ret = -ENOMEM;
		goto put_out;
	}

	/* If we are in generate mode, we do not get the value from userspace */
	if (!(flags & TCTRL_GENERATE_BIT)) {
		if (copy_from_user(value, put_request.value, value_len)) {
			pr_debug("PUT: copying value to user failed");
			ret = -EINVAL;		
			goto put_out;
		}
	}

	/* If the dummy bit is set, skip the actual operation */
	if (flags & TCTRL_DUMMY_BIT)
		goto put_out;

	if (tmem_put(key, key_len, value, value_len) < 0) {
		pr_debug("TMEM_PUT command failed");
		ret = -EINVAL;
	}

put_out:
	kfree(key);
	
	return ret;
}


int tmem_chrdev_get(struct tmem_dev *tmem_dev, struct tmem_get_request get_request, long flags) {

	void *key, *value;
	size_t key_len, value_len = 10;
	int ret = 0;


	key_len = get_request.key_len;
	ret = get_key(&key, get_request.key, key_len);
	if (ret < 0) 
		goto get_out;

	value = tmem_dev->buf;

	/* Only actually do the operation if not in dummy or generate mode */
	if (!(flags & (TCTRL_DUMMY_BIT | TCTRL_GENERATE_BIT))) {
		ret = tmem_get(key, key_len, value, &value_len); 
		if (ret < 0 && ret != -EINVAL)
			goto get_out;
	}

	if (flags & TCTRL_GENERATE_BIT) 
		value_len = tmem_dev->generated_size;
	

	/* In case the key is not in the store, or we are in silent or dummy mode, we return a value of length 0 */
	if (ret == -EINVAL || (flags & (TCTRL_DUMMY_BIT | TCTRL_SILENT_BIT))) {
		ret = 0;
		value_len = 0;
	} 
	
	if (copy_to_user(get_request.value_lenp, &value_len, sizeof(value_len))) {
		ret = -EINVAL;
		goto get_out;
	}

	if (copy_to_user(get_request.value, value, value_len)) 
		ret = -EINVAL;
get_out:

	kfree(key);

	return ret;

}

int tmem_chrdev_inval(struct tmem_invalidate_request invalidate_request, long flags) {

	void *key;
	size_t key_len;
	int ret;

	key_len = invalidate_request.key_len;
	ret = get_key(&key, invalidate_request.key, key_len);
	if (ret < 0) 
		return ret;
	
	if (flags & TCTRL_DUMMY_BIT)
		goto inval_out;

	
	tmem_invalidate(key, key_len);

inval_out:

	kfree(key);

	return 0;

}


long tmem_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct tmem_dev *tmem_dev;
	struct tmem_request tmem_request;
	long flags;
	int ret = 0;

	tmem_dev = (struct tmem_dev *) filp->private_data;

	if (down_trylock(&lock)) {
		return -EBUSY;
	}


	if (cmd == TMEM_CONTROL) {
		tmem_request.flags = arg;
	} else if (cmd != TMEM_GENERATE_SIZE) { 
		if (copy_from_user(&tmem_request, (struct tmem_request *) arg, sizeof(tmem_request))) 
			return -ERESTARTSYS;	
	} else {
		tmem_request.flags = 0;
	}

	/* If the request has a nonzero flags argument, override the settings of the device */
	if (tmem_request.flags)
		flags = tmem_request.flags;
	else
		flags = tmem_dev->flags;

		
	if (flags & TCTRL_SLEEPY_BIT) 
		usleep_range(SLEEP_USECS - SLEEP_USECS_SLACK, SLEEP_USECS + SLEEP_USECS_SLACK);
	


	switch (cmd) {
	case TMEM_GET:	
	
		ret = tmem_chrdev_get(tmem_dev, tmem_request.get, flags);
		goto ioctl_out;

	case TMEM_PUT:

		ret = tmem_chrdev_put(tmem_dev, tmem_request.put, flags);
		goto ioctl_out;

	case TMEM_INVAL:

		ret = tmem_chrdev_inval(tmem_request.inval, flags);
		goto ioctl_out;

	case TMEM_CONTROL:

		/* Control whether the backend will actually do anything */
		if (arg & (TCTRL_DUMMY)) 
			tmem_dev->flags |= TCTRL_DUMMY_BIT;	

		if (arg & (TCTRL_REAL))
			tmem_dev->flags &= ~ TCTRL_DUMMY_BIT;				

		/* Control whether the backend will sleep for some us before commencing with the operation */
		if (arg & (TCTRL_SLEEPY))
			tmem_dev->flags |= TCTRL_SLEEPY_BIT;
				
		if (arg & (TCTRL_AWAKE))
			tmem_dev->flags &= ~ TCTRL_SLEEPY_BIT;

		/* Control whether the backend will copy_to_user() the result (where applicable) */
		if (arg & (TCTRL_SILENT)) 
			tmem_dev->flags |= TCTRL_SILENT_BIT;	
				
		if (arg & (TCTRL_ANSWER))
			tmem_dev->flags &= ~ TCTRL_SILENT_BIT;

		/* Control whether the backend will generate the result */
		if (arg & (TCTRL_GENERATE)) 
			tmem_dev->flags |= TCTRL_GENERATE_BIT;	
				
		if (arg & (TCTRL_INPUT))
			tmem_dev->flags &= ~ TCTRL_GENERATE_BIT;

		ret = 0;
		goto ioctl_out;

	case TMEM_GENERATE_SIZE:
		
		if (arg < 0) {
			ret = -EINVAL;
			goto ioctl_out;
		}

		tmem_dev->generated_size = (size_t) arg;
			
		ret = 0;
		goto ioctl_out;
	default:

		ret = -ENOSYS;
		goto ioctl_out;
	}

ioctl_out:
	up(&lock);

	return ret;
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

	pr_err("IOCTL Numbers for get, put, invaliuate, control: %lu %lu %lu %lu\n",
		TMEM_GET, TMEM_PUT, TMEM_INVAL, TMEM_CONTROL);

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

