#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <tmem/tmem_ops.h> 

#ifdef CONFIG_DEBUG_FS
static u64 tmem_put_counter;
static u64 tmem_get_counter;
static u64 tmem_control_counter;
static u64 tmem_invalidate_counter;
static u64 tmem_generate_counter;

static u64 hcall_put_counter;
static u64 hcall_get_counter;
static u64 hcall_invalidate_counter;

static inline void inc_tmem_put(void){ 
	tmem_put_counter++; 
}

static inline void inc_tmem_get(void){ 
	tmem_get_counter++; 
}

static inline void inc_tmem_control(void){ 
	tmem_control_counter++; 
}

static inline void inc_tmem_invalidate(void){ 
	tmem_invalidate_counter++; 
}


static inline void inc_tmem_generate(void){ 
	tmem_generate_counter++; 
}

static inline void inc_hcall_put(void){ 
	hcall_put_counter++; 
}

static inline void inc_hcall_get(void){ 
	hcall_get_counter++; 
}

static inline void inc_hcall_invalidate(void){ 
	hcall_invalidate_counter++; 
}

#else
static inline void inc_tmem_put(void) {} 
static inline void inc_tmem_get(void) {} 
static inline void inc_tmem_control(void) {} 
static inline void inc_tmem_invalidate(void) {} 
static inline void inc_tmem_generate(void) {} 
static inline void inc_hcall_put(void) {} 
static inline void inc_hcall_get(void) {} 
static inline void inc_hcall_invalidate(void) {}

#endif /* CONFIG_DEBUG_FS */


struct tmem_dev {
	void *buf;
	u64 flags;
	u64 generated_size;
};

struct tmem_dev *tmem_dev;

/* 
 * This can be removed, if we assign "namespaces" to each 
 * process opening it
 */
DEFINE_SEMAPHORE(lock); 

int tmem_chrdev_open(struct inode *inode, struct file *filp)
{
	filp->private_data = tmem_dev;

	return 0;
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


	inc_tmem_put();	

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

	inc_hcall_put();	


put_out:
	kfree(key);
	
	return ret;
}


int tmem_chrdev_get(struct tmem_dev *tmem_dev, struct tmem_get_request get_request, long flags) {

	void *key, *value;
	size_t key_len, value_len;
	int ret = 0;


	inc_tmem_get();	


	key_len = get_request.key_len;
	ret = get_key(&key, get_request.key, key_len);
	if (ret < 0) 
		goto get_out;

	value = tmem_dev->buf;

	/* Only actually do the operation if not in dummy or generate mode */
	if (!(flags & (TCTRL_DUMMY_BIT | TCTRL_GENERATE_BIT))) {
		ret = tmem_get(key, key_len, value, &value_len); 

		inc_hcall_get();	

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


	inc_tmem_invalidate();	

	key_len = invalidate_request.key_len;
	ret = get_key(&key, invalidate_request.key, key_len);
	if (ret < 0) 
		return ret;
	
	if (flags & TCTRL_DUMMY_BIT)
		goto inval_out;

	
	tmem_invalidate(key, key_len);

	inc_hcall_invalidate();	


inval_out:

	kfree(key);

	return 0;

}


long tmem_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct tmem_dev *tmem_dev;
	struct tmem_request tmem_request;
	long __user * usrflags;
	size_t __user *usrgensize;
	size_t gensize;
	long flags;
	int ret = 0;

	tmem_dev = (struct tmem_dev *) filp->private_data;

	if (down_trylock(&lock)) {
		return -EBUSY;
	}


	/* There only is a request for calls corresponding to real tmem ops*/
	if (cmd != TMEM_GENERATE_SIZE && cmd != TMEM_CONTROL) { 
		if (copy_from_user(&tmem_request, (struct tmem_request *) arg, sizeof(tmem_request))) 
			return -ERESTARTSYS;	
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
		inc_tmem_control();	

		usrflags = (__user long *) arg;
		ret = get_user(flags, usrflags);
		if (ret)
			goto ioctl_out;

		/* Control whether the backend will actually do anything */
		if (flags & (TCTRL_DUMMY)) 
			tmem_dev->flags |= TCTRL_DUMMY_BIT;	

		if (flags & (TCTRL_REAL))
			tmem_dev->flags &= ~ TCTRL_DUMMY_BIT;				

		/* Control whether the backend will sleep for some us before commencing with the operation */
		if (flags & (TCTRL_SLEEPY))
			tmem_dev->flags |= TCTRL_SLEEPY_BIT;
				
		if (flags & (TCTRL_AWAKE))
			tmem_dev->flags &= ~ TCTRL_SLEEPY_BIT;

		/* Control whether the backend will copy_to_user() the result (where applicable) */
		if (flags & (TCTRL_SILENT)) 
			tmem_dev->flags |= TCTRL_SILENT_BIT;	
				
		if (flags & (TCTRL_ANSWER))
			tmem_dev->flags &= ~ TCTRL_SILENT_BIT;

		/* Control whether the backend will generate the result */
		if (flags & (TCTRL_GENERATE)) 
			tmem_dev->flags |= TCTRL_GENERATE_BIT;	
				
		if (flags & (TCTRL_INPUT))
			tmem_dev->flags &= ~ TCTRL_GENERATE_BIT;

		ret = 0;
		goto ioctl_out;

	case TMEM_GENERATE_SIZE:
		inc_tmem_generate();	

		usrgensize = (__user size_t *) arg;
		ret = get_user(gensize, usrgensize);
		if (ret)
			goto ioctl_out;
		
		if (gensize < 0) {
			ret = -EINVAL;
			goto ioctl_out;
		}

		tmem_dev->generated_size = (size_t) gensize;
			
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
	struct dentry *root; 

	pr_err("IOCTL Numbers for get, put, invalidate, control: %lu %lu %lu %lu\n",
		TMEM_GET, TMEM_PUT, TMEM_INVAL, TMEM_CONTROL);

	/* Allocation and Initialization of the global tmem_dev, shared among files */
	tmem_dev = kmalloc(sizeof(struct tmem_dev), GFP_KERNEL);
	if (!tmem_dev) {
		return -ENOMEM;
	}

	tmem_dev->buf = kmalloc(TMEM_MAX, GFP_KERNEL);
	if (!tmem_dev->buf) {
		kfree(tmem_dev);
		return -ENOMEM;
	}

	tmem_dev->flags = 0x00000000;
	tmem_dev->generated_size = 0;


	/* Device registration */
	ret = misc_register(&tmem_chrdev);
	if (ret) 
		goto register_err;
	

#ifdef CONFIG_DEBUG_FS
	
	root = debugfs_create_dir("tmem_dev", NULL);
	if (!root) 
		goto debugfs_err;

	debugfs_create_u64("puts", S_IRUGO, root, &tmem_put_counter);
	debugfs_create_u64("gets", S_IRUGO, root, &tmem_get_counter);
	debugfs_create_u64("invalidates", S_IRUGO, root, &tmem_invalidate_counter);
	debugfs_create_u64("controls", S_IRUGO, root, &tmem_control_counter);
	debugfs_create_u64("generates", S_IRUGO, root, &tmem_generate_counter);
	debugfs_create_u64("hcall_puts", S_IRUGO, root, &hcall_put_counter);
	debugfs_create_u64("hcall_gets", S_IRUGO, root, &hcall_get_counter);
	debugfs_create_u64("hcall_invalidates", S_IRUGO, root, &hcall_invalidate_counter);
	debugfs_create_x64("flags", S_IRUGO, root, &tmem_dev->flags);
	debugfs_create_u64("gensize", S_IRUGO, root, &tmem_dev->generated_size);

#endif /* CONFIG_DEBUG_FS */

	return 0;


debugfs_err:

	misc_deregister(&tmem_chrdev); 
	ret = -ENXIO;

register_err:

	pr_err("Device registration failed\n");
	return ret;
}


static void __exit exit_func(void)
{
	misc_deregister(&tmem_chrdev);
	kfree(tmem_dev->buf);
	kfree(tmem_dev);
}


module_init(init_func);
module_exit(exit_func);

MODULE_LICENSE("GPL");

