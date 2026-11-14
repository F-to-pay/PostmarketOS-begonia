# Mainline drivers/media/i2c/ov8856.c unchanged, out of tree (CONFIG_VIDEO_OV8856 is disabled in the phone config).
# The source is taken from the kernel tree: make gen
obj-m += ov8856.o
KDIR ?= $(HOME)/mt6785-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules

gen:
	cp $(KDIR)/drivers/media/i2c/ov8856.c .

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
