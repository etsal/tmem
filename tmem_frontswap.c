#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/frontswap.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>

#include <tmem/tmem_ops.h>

/* 
 * This is needed because we need to pass values held in the kernel's 
 * pages to the tmem_* functions, and offset is in the stack
 */
static pgoff_t *key;

static int tmem_frontswap_store(unsigned int type, pgoff_t offset,
				struct page *page)
{
	void *value= (void *) page_address(page);

	memcpy(key, &offset, sizeof(offset));
	return tmem_put(key, sizeof(key), value, PAGE_SIZE);
}

static int tmem_frontswap_load(unsigned int type, pgoff_t offset,
				struct page *page)
{
	void *value= (void *) page_address(page);
	/* In frontswap we already know the length of the value*/
	size_t ignored;

	memcpy(key, &offset, sizeof(offset));
	return tmem_get(key, sizeof(key), value, &ignored);
}

static void tmem_frontswap_invalidate_page(unsigned int type, pgoff_t offset)
{
	memcpy(key, &offset, sizeof(offset));
	tmem_invalidate(key, sizeof(key));
}

static void tmem_frontswap_invalidate_area(unsigned int type)
{
	tmem_invalidate_area();
}

static void tmem_frontswap_init(unsigned int ignored)
{
}

static struct frontswap_ops tmem_frontswap_ops = {
	.store = tmem_frontswap_store,
	.load = tmem_frontswap_load,
	.invalidate_page = tmem_frontswap_invalidate_page,
	.invalidate_area = tmem_frontswap_invalidate_area,
	.init = tmem_frontswap_init,
};

static int __init tmem_init(void)
{
	frontswap_writethrough(false);
	frontswap_register_ops(&tmem_frontswap_ops);
	pr_debug("registration successful");

	key = kmalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	return 0;
}



module_init(tmem_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
