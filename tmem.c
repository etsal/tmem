#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/mm.h>

#include "tmem.h"

static u64 current_memory; 

struct page_list {
	struct hlist_node hash_node;
	struct tmem_key tmem_key;
	void *value;
    size_t len;
};

DEFINE_SPINLOCK(used_lock); 
DEFINE_HASHTABLE(used_pages, 10);   

#define TMEM_POOL_ID (0) 
#define TMEM_OBJ_ID (0) 
#define TMEM_POOL_SIZE (64 * 1024 * 1024) 

int tmem_put_page(struct page *page, struct tmem_key tmem_key, size_t len)
{
	struct page_list *page_entry = NULL;
	int already_exists = 0;
	unsigned long flags;
    void *key = tmem_key.key;
    size_t key_len = tmem_key.key_len;
    int ret = -1;

	pr_debug("entering put_page\n");


	/* If the page already exists, update it */
	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(long *) key) {
        /* TODO: Is this correct? The lengths seem weird */
		if (!memcmp(page_entry->tmem_key.key, key, min(tmem_key.key_len, key_len))) { 
			already_exists = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&used_lock, flags);

	/* Or else get a new one */
	if (!already_exists) {	

        if (current_memory + PAGE_SIZE > TMEM_POOL_SIZE) 
            goto out_pool;
	
		page_entry = kzalloc(sizeof(*page_entry), GFP_KERNEL);
		if (!page_entry)
			goto out_mem;
        
        page_entry->tmem_key.key = kmalloc(key_len, GFP_KERNEL);
        if(!page_entry->tmem_key.key)
            goto out_mem;

        page_entry->value = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!page_entry->value)
			goto out_mem;
	
	}

	memcpy(page_entry->tmem_key.key, key, key_len);
    page_entry->tmem_key.key_len = key_len;
	memcpy(page_entry->value, (void *) page_address(page), min(len, PAGE_SIZE));
    page_entry->len = len;

	if(!already_exists){
		spin_lock_irqsave(&used_lock, flags);
		hash_add(used_pages, &page_entry->hash_node, *(long *)key);
		spin_unlock_irqrestore(&used_lock, flags);
	
		current_memory += PAGE_SIZE;
	}
	
	pr_debug("leaving put_page\n");
	
	return 0;

out_mem:

    if (page_entry->value)
        kfree(page_entry->value);

    if (page_entry->tmem_key.key)
        kfree(page_entry->tmem_key.key);

    if (page_entry)
        kfree(page_entry);

    pr_err("leaving put_page - not enough memory\n");
    
    ret = -ENOMEM;

out_pool:

    pr_debug("leaving put_page - failed\n");
    
    return ret;
}
EXPORT_SYMBOL(tmem_put_page);


int tmem_get_page(struct page *page, struct tmem_key tmem_key, size_t *len)
{
	struct page_list *page_entry;
	unsigned long flags;
    void *key = tmem_key.key;
    size_t key_len = tmem_key.key_len;

	pr_debug("entering get_page\n");
	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(long *) key) {
		if (!memcmp(page_entry->tmem_key.key, key, min(tmem_key.key_len, key_len))) {

            *len = page_entry->len;
			memcpy((void *) page_address(page), page_entry->value, min(*len, PAGE_SIZE));
			spin_unlock_irqrestore(&used_lock, flags);

			pr_debug("leaving get_page\n");

			return 0;
		}
	}

	spin_unlock_irqrestore(&used_lock, flags);
	pr_warn("leaving get_page - failed\n");

	return -EINVAL;
}
EXPORT_SYMBOL(tmem_get_page);

void tmem_invalidate_page(struct tmem_key tmem_key)
{
	struct page_list *page_entry;
	unsigned long flags;
    void *key = tmem_key.key;
    size_t key_len = tmem_key.key_len;

	pr_debug("entering invalidate_page\n");

	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(long *) key) {
		if (!memcmp(page_entry->tmem_key.key, key, min(tmem_key.key_len, key_len))) {
			hash_del(&page_entry->hash_node);
			spin_unlock_irqrestore(&used_lock, flags);

            kfree(page_entry->value);
            kfree(page_entry->tmem_key.key);
            kfree(page_entry);


			pr_debug("leaving invalidate_page\n");

			current_memory -= PAGE_SIZE;

			return;
		}
	}
	spin_unlock_irqrestore(&used_lock, flags);
	pr_debug("leaving invalidate_page - key not present\n");

	return;

}
EXPORT_SYMBOL(tmem_invalidate_page);


void tmem_invalidate_area(void)
{
	struct page_list *page_entry;
	unsigned long flags;
	int bkt;

	pr_debug("entering invalidate_area\n");

	spin_lock_irqsave(&used_lock, flags);
	hash_for_each(used_pages, bkt, page_entry, hash_node) {
		hash_del(&page_entry->hash_node);

        kfree(page_entry->tmem_key.key);
        kfree(page_entry->value);
        kfree(page_entry);

	}
	spin_unlock_irqrestore(&used_lock, flags);
	pr_debug("leaving invalidate_area\n");
}
EXPORT_SYMBOL(tmem_invalidate_area);


static int __init tmem_init(void)
{
	struct dentry *root;

    current_memory = 0;
	hash_init(used_pages);

	root = debugfs_create_dir("tmem", NULL);
	if (root == NULL) {
		pr_err("debugfs directory could not be set up\n");
		goto out;
	}

	if (!debugfs_create_u64("current_memory", S_IRUGO, root, &current_memory)) 
        pr_err("debugfs entry could not be set up\n");

out:

	return 0;
    
}



module_init(tmem_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
