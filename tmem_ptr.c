
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

#include <tmem/tmem_ops.h> 

static u64 current_memory; 

struct page_list {
	struct hlist_node hash_node;
	void *key;
	size_t key_len;
	void *value;
	size_t value_len;
};

DEFINE_SPINLOCK(used_lock); 
DEFINE_HASHTABLE(used_pages, 10);   

#define TMEM_POOL_ID (0) 
#define TMEM_OBJ_ID (0) 
#define TMEM_POOL_SIZE (1024 * 1024 * 1024) 

int tmem_ptr_put_page(void *key, size_t key_len, void *value, size_t value_len)
{
	struct page_list *page_entry = NULL;
	int already_exists = 0;
	unsigned long flags;
	int ret = -1;

//	pr_debug("entering put_page\n");
/*
	pr_err("PUT: Key %s", (char *) key);
	pr_err("PUT: Value %s", (char *) value);
*/
	/* If the page already exists, update it */
	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(char *) key) {
		if (page_entry->key_len == key_len && !memcmp(page_entry->key, key, key_len)) {
			already_exists = 1;
			break;
		}
		/*
		pr_err("GET: Key %s", (char *) page_entry->key);
		pr_err("GET: Value %s", (char *) page_entry->value);
		pr_err("GET: Keylen %lu", page_entry->key_len);
		*/	
	}
	spin_unlock_irqrestore(&used_lock, flags);

	/* Or else get a new one */
	if (!already_exists) {	
//		pr_err("Doesn't already exist");
/*
		if (current_memory + PAGE_SIZE > TMEM_POOL_SIZE) 
		    goto out_pool;
*/
		
		page_entry = kzalloc(sizeof(*page_entry), GFP_KERNEL);
		if (!page_entry)
			goto out_mem;

		page_entry->key = key;
		page_entry->key_len = key_len;
        
	} else {

		//pr_err("Already exists");
		kfree(key);
		kfree(page_entry->value);

	}

	page_entry->value = value;
	page_entry->value_len = value_len;


	if(!already_exists){
		spin_lock_irqsave(&used_lock, flags);
		hash_add(used_pages, &page_entry->hash_node, *(char *)key);
		spin_unlock_irqrestore(&used_lock, flags);
	
		/* Turn off accounting for now */
		current_memory += 0;
	}
	
	pr_debug("leaving put_page\n");
	
	return 0;

out_mem:

    kfree(key);
    kfree(value);

    if (page_entry)
        kfree(page_entry);

    pr_err("leaving put_page - not enough memory\n");
    
    ret = -ENOMEM;

out_pool:

    pr_debug("leaving put_page - failed\n");
    
    return ret;
}


int tmem_ptr_get_page(void *key, size_t key_len, void *value, size_t *value_len)
{
	struct page_list *page_entry;
	unsigned long flags;
	unsigned long *address = (unsigned long *) value;

	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(char *) key) {
		if (page_entry->key_len == key_len && !memcmp(page_entry->key, key, key_len)) {

			*value_len = page_entry->value_len;
			*address = (unsigned long) page_entry->value;

			spin_unlock_irqrestore(&used_lock, flags);

			kfree(key);

			return 0;
		}
		/*
		pr_err("GET: Key %s", (char *) page_entry->key);
		pr_err("GET: Value %s", (char *) page_entry->value);
		pr_err("GET: Keylen %lu", page_entry->key_len);
		*/
	}

	spin_unlock_irqrestore(&used_lock, flags);

	*address = (long) NULL;
	*value_len = 0;

	kfree(key);

	return -EINVAL;
}

void tmem_ptr_invalidate_page(void *key, size_t key_len)
{
	struct page_list *page_entry;
	unsigned long flags;

	pr_debug("entering invalidate_page\n");

	spin_lock_irqsave(&used_lock, flags);
	hash_for_each_possible(used_pages, page_entry, hash_node, *(char *) key) {
		if (!memcmp(page_entry->key, key, min(page_entry->key_len, key_len))) {
			hash_del(&page_entry->hash_node);
			spin_unlock_irqrestore(&used_lock, flags);

			kfree(page_entry->value);
			kfree(page_entry->key);
			kfree(page_entry);

			kfree(key);

			//pr_debug("leaving invalidate_page\n");

			//current_memory -= PAGE_SIZE;

			return;
		}
	}

	kfree(key);

	spin_unlock_irqrestore(&used_lock, flags);
	pr_debug("leaving invalidate_page - key not present\n");

	return;

}


void tmem_ptr_invalidate_area(void)
{
	struct page_list *page_entry;
	unsigned long flags;
	int bkt;

	pr_debug("entering invalidate_area\n");

	spin_lock_irqsave(&used_lock, flags);
	hash_for_each(used_pages, bkt, page_entry, hash_node) {
		hash_del(&page_entry->hash_node);

		kfree(page_entry->key);
		kfree(page_entry->value);
		kfree(page_entry);

	}
	spin_unlock_irqrestore(&used_lock, flags);
	pr_debug("leaving invalidate_area\n");
}

struct tmem_ops tmem_naive_ops = {
	.get = tmem_ptr_get_page,
	.put = tmem_ptr_put_page,
	.invalidate = tmem_ptr_invalidate_page,
	.invalidate_all = tmem_ptr_invalidate_area,
};

static int __init tmem_ptr_init(void)
{
	struct dentry *root;

	current_memory = 0;
	hash_init(used_pages);

	register_tmem_ops(&tmem_naive_ops);	

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



module_init(tmem_ptr_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
