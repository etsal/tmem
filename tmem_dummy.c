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


#define TMEM_POOL_ID (0) 
#define TMEM_OBJ_ID (0) 
#define TMEM_POOL_SIZE (64 * 1024 * 1024) 


static u64 current_memory; 

int tmem_dummy_put_page(struct page *page, struct tmem_key tmem_key, size_t len)
{
	
	return 0;
}

int tmem_dummy_get_page(struct page *page, struct tmem_key tmem_key, size_t *len)
{
	*len = 0;
	return 0;
}

void tmem_dummy_invalidate_page(struct tmem_key tmem_key)
{
}

void tmem_dummy_invalidate_area(void) {

//	create_control_page(tmem_key, *len);
//	dummy_hypercall3(KVM_HC_TMEM, PV_TMEM_INVALIDATE_OP, page_to_pfn(*page), page_to_pfn(*control_page));

}

struct tmem_ops tmem_dummy_ops = {
	.get = tmem_dummy_get_page,
	.put = tmem_dummy_put_page,
	.invalidate = tmem_dummy_invalidate_page,
	.invalidate_all = tmem_dummy_invalidate_area,
};

static int __init tmem_dummy_init(void)
{
	struct dentry *root;

	current_memory = 0;

	register_tmem_ops(&tmem_dummy_ops);	

	root = debugfs_create_dir("tmem", NULL);
	if (!root) {
		pr_err("debugfs directory could not be set up\n");
		goto out;
	}

	if (!debugfs_create_u64("current_memory", S_IRUGO, root, &current_memory)) 
		pr_err("debugfs entry could not be set up\n");

out:

	return 0;
    
}



module_init(tmem_dummy_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
