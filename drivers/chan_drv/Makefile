obj-m += chan_drv.o

KDIR ?= /project/25-linuxKernel2/linux
OUT  ?= /project/25-linuxKernel2/out

all:
	$(MAKE) -C $(KDIR) O=$(OUT) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) O=$(OUT) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=$(CURDIR) clean
