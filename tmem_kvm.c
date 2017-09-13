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
#include <uapi/linux/kvm_para.h>


#define TMEM_POOL_ID (0) 
#define TMEM_OBJ_ID (0) 
#define TMEM_POOL_SIZE (64 * 1024 * 1024) 


static u64 current_memory; 

static struct page *control_page = NULL;

void create_control_page(struct tmem_key tmem_key, struct page *page, size_t len)
{
	struct key_value key_value;

	key_value.key = tmem_key.key;
	key_value.key_len = tmem_key.key_len;
	key_value.value = page_address(page);
	key_value.value_len = len;

	memcpy(page_address(control_page), &key_value, sizeof(key_value));
	memcpy(page_address(control_page) + sizeof(key_value), key_value.key, key_value.key_len);
	
}

int tmem_kvm_put_page(struct page *page, struct tmem_key tmem_key, size_t len)
{
	
	create_control_page(tmem_key, page, len);
	return kvm_hypercall3(KVM_HC_TMEM, PV_TMEM_PUT_OP, page_to_pfn(control_page), page_to_pfn(page));
}

int tmem_kvm_get_page(struct page *page, struct tmem_key tmem_key, size_t *len)
{
	int ret;

	create_control_page(tmem_key, page, *len);
	ret = kvm_hypercall3(KVM_HC_TMEM, PV_TMEM_GET_OP, page_to_pfn(control_page), page_to_pfn(page));
	*len = *((size_t *) page_to_virt(control_page));

	return ret;
}

void tmem_kvm_invalidate_page(struct tmem_key tmem_key)
{

	create_control_page(tmem_key, NULL, 0);
	kvm_hypercall3(KVM_HC_TMEM, PV_TMEM_INVALIDATE_OP, page_to_pfn(control_page), 0);

}

void tmem_kvm_invalidate_area(void) {

//	create_control_page(tmem_key, *len);
//	kvm_hypercall3(KVM_HC_TMEM, PV_TMEM_INVALIDATE_OP, page_to_pfn(*page), page_to_pfn(*control_page));

}

struct tmem_ops tmem_kvm_ops = {
	.get = tmem_kvm_get_page,
	.put = tmem_kvm_put_page,
	.invalidate = tmem_kvm_invalidate_page,
	.invalidate_all = tmem_kvm_invalidate_area,
};

static int __init tmem_kvm_init(void)
{
	struct dentry *root;

	current_memory = 0;
	control_page = alloc_page(GFP_KERNEL);
	if (!control_page) {
		pr_err("allocation of local buffer failed");
		goto out;
	}

	register_tmem_ops(&tmem_kvm_ops);	

	root = debugfs_create_dir("tmem", NULL);
	if (!root) {
		pr_err("debugfs directory could not be set up\n");
		goto out;
	}

	if (!debugfs_create_u64("current_memory", S_IRUGO, root, &current_memory)) 
		pr_err("debugfs entry could not be set up\n");

	return 0;

out:
	if (control_page)
		free_page((long) control_page);

	return 0;
    
}



module_init(tmem_kvm_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
