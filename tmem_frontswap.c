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

static int tmem_frontswap_store(unsigned int type, pgoff_t offset,
				struct page *page)
{
	void *value= (void *) page_address(page);

	return tmem_put(&offset, sizeof(offset), value, PAGE_SIZE);
}

static int tmem_frontswap_load(unsigned int type, pgoff_t offset,
				struct page *page)
{
	void *value= (void *) page_address(page);
	/* In frontswap we already know the length of the value*/
	size_t ignored;

	return tmem_get(&offset, sizeof(offset), value, &ignored);
}

static void tmem_frontswap_invalidate_page(unsigned int type, pgoff_t offset)
{
	tmem_invalidate(&offset, sizeof(offset));
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

	return 0;
}



module_init(tmem_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
