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

/* 
 * This can be removed, if we assign "namespaces" to each 
 * process opening it
 */
DEFINE_SEMAPHORE(lock); 

int tmem_chrdev_open(struct inode *inode, struct file *filp)
{
	struct page *page;

	if (down_trylock(&lock))
		return -EBUSY;


	page = alloc_page(GFP_KERNEL);
	if (!page) {
		up(&lock);
		return -ENOMEM;
	}
	filp->private_data = page;

	return 0;
}

int tmem_chrdev_release(struct inode *inode, struct file *filp)
{
	__free_page((struct page *) filp->private_data);
	up(&lock);

	return 0;
}


long tmem_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void *page = filp->private_data;
    struct key_value pair;
    size_t len;
    void *key;
    struct tmem_key tmem_key;


    if (copy_from_user(&pair, (struct key_value *) arg, sizeof(struct key_value))) 
        return -ERESTARTSYS;

    /* The key is a long (for now), so expand it to that size*/
    key = kmalloc(max(pair.key_len, sizeof(long)), GFP_KERNEL);
    if (!key)
        return -ENOMEM;

    if (copy_from_user(key, pair.key, pair.key_len)) {
        kfree(key); 
        return -ERESTARTSYS;
    }

    /* The key needs to be the same every time, so zero out any garbage after it */
    if (sizeof(long) > pair.key_len)
        memset(key + pair.key_len, 0, sizeof(long) - pair.key_len);


    tmem_key.key = key;
    tmem_key.key_len = pair.key_len;

	switch (cmd) {
	case TMEM_GET:
		pr_debug("got into get");

		if (tmem_get(page, tmem_key, &len) < 0) {
			pr_err("TMEM_GET command failed");
			return -EINVAL;
		}

		if (copy_to_user(pair.value, (void *) page_address(page), len)) {
			pr_err("copying to user failed");
			return -EINVAL;
		}

		if (copy_to_user((struct key_value *) arg, &pair, sizeof(struct key_value))) {
			pr_err("copying to user failed");
			return -EINVAL;
		}

		break;

	case TMEM_PUT:
		pr_debug("got into put");


		if (copy_from_user((void *) page_address(page), pair.value, pair.value_len)) {
			pr_debug("copying to user failed");
			return -EINVAL;
		}

		if (tmem_put(page, tmem_key, pair.value_len) < 0) {
			pr_debug("TMEM_PUT command failed");
			return -EINVAL;
		}

		break;

	case TMEM_INVAL:
		pr_debug("Got into invalidate");
		tmem_invalidate(tmem_key);

		break;


	default:

		pr_err("illegal argument");
		return -EINVAL;
	}

    kfree(tmem_key.key);

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

