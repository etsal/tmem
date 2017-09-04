#ifndef _TMEM_H
#define _TMEM_H

#include <uapi/asm-generic/ioctl.h>

#define TMEM_MAGIC ('*') 

#define TMEM_GET (_IOW(TMEM_MAGIC, 1, long))
#define TMEM_PUT (_IOR(TMEM_MAGIC, 2, long))
#define TMEM_INVAL (_IO(TMEM_MAGIC, 3))

struct key_value {
    void *key;
    size_t key_len; 
    void *value;
    size_t *value_len; 
};

struct tmem_key {
    void *key;
    size_t key_len;
};

#define DECLARE_TMEM_KEY(name, key_name, key_length) \
    struct tmem_key name = { \
            .key = key_name, \
            .key_len = key_length } 

extern int tmem_put_page(struct page *, struct tmem_key, size_t);
extern int tmem_get_page(struct page *, struct tmem_key, size_t *);
extern void tmem_invalidate_page(struct tmem_key);
extern void tmem_invalidate_area(void);

#endif /* _TMEM_H */
