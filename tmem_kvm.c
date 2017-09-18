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
#include <asm/page.h>

#include <tmem/tmem_ops.h> 
#include <uapi/linux/kvm_para.h>


#define TMEM_POOL_ID (0) 
#define TMEM_OBJ_ID (0) 
#define TMEM_POOL_SIZE (64 * 1024 * 1024) 


static u64 current_memory; 
static struct tmem_request request;

struct page *page = NULL;
size_t *value_len_ptr = NULL;


int tmem_kvm_put_page(void *key, size_t key_len, void *value, size_t value_len)
{
	int ret;

	struct tmem_put_request put_request = {
		.key = (void *) virt_to_phys(key),
		.key_len = key_len,
		.value = (void *) virt_to_phys(value),
		.value_len = value_len,
	};
	request.put = put_request;
	
	*((struct tmem_request *)(page_to_virt(page))) = request;


	ret = kvm_hypercall2(KVM_HC_TMEM, PV_TMEM_PUT_OP, page_to_phys(page));
	if (ret)
		pr_err("Hypercall failed");
	return ret;
}

int tmem_kvm_get_page(void *key, size_t key_len, void *value, size_t *value_lenp)
{
	int ret;
	struct tmem_request request;

	*value_len_ptr = *value_lenp;

	struct tmem_get_request get_request = {
		.key = (void *) virt_to_phys(key),
		.key_len = key_len,
		.value = (void *) virt_to_phys(value),
		.value_lenp = (void *) virt_to_phys(value_len_ptr),
	};
	request.get = get_request;

	*((struct tmem_request *)(page_to_virt(page))) = request;

	ret = kvm_hypercall2(KVM_HC_TMEM, PV_TMEM_GET_OP, page_to_phys(page));
	if (ret && ret != -EINVAL)
		pr_err("Hypercall failed");

	*value_lenp = *value_len_ptr;

	return ret;

}

void tmem_kvm_invalidate_page(void *key, size_t key_len)
{
	int ret;
	struct tmem_request request;


	struct tmem_invalidate_request invalidate_request = {
		.key = (void *) virt_to_phys(key),
		.key_len = key_len,
	};
	request.inval = invalidate_request;

	*((struct tmem_request *)(page_to_virt(page))) = request;

	ret = kvm_hypercall2(KVM_HC_TMEM, PV_TMEM_INVALIDATE_OP, page_to_phys(page));
	if (ret)
		pr_err("Hypercall failed");
}

void tmem_kvm_invalidate_area(void) {


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

	page = alloc_page(GFP_KERNEL);
	value_len_ptr = kmalloc(sizeof(size_t), GFP_KERNEL);
	if (!page || !value_len_ptr)
		goto out_fail;

	current_memory = 0;

	register_tmem_ops(&tmem_kvm_ops);	

	root = debugfs_create_dir("tmem", NULL);
	if (!root) {
		pr_err("debugfs directory could not be set up\n");
		goto out;
	}

	if (!debugfs_create_u64("current_memory", S_IRUGO, root, &current_memory)) 
		pr_err("debugfs entry could not be set up\n");

out:

	return 0;

out_fail:

	if (page)
		__free_page(page);

	if (value_len_ptr)
		kfree(value_len_ptr);
    
	return -ENOMEM;
}



module_init(tmem_kvm_init);
MODULE_AUTHOR("Aimilios Tsalapatis");
MODULE_LICENSE("GPL");
