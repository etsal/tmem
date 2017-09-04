#If the environment variable is set, no extra info is required
ifneq ($(KERNELRELEASE),)
	obj-m += tmem.o tmem_dev.o tmem_frontswap.o
	#If it isn't, use the shell to find the kernel version and the directory
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

	#In any case, create the module
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.ko *.o *.mod.c Module.symvers modules.builtin modules.order
