obj-m += systolic_mm_kernel_driver.o

KDIR := $(HOME)/buildroot/output/build/linux-6.18.7
ARCH := riscv
CROSS_COMPILE := $(HOME)/buildroot/output/host/bin/riscv64-buildroot-linux-gnu-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
