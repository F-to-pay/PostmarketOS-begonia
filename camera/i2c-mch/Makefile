# Out-of-tree i2c-mt65xx-mch (MT6785 multi-channel I2C). The source is generated: make gen (needs the kernel tree).
obj-m += i2c-mt65xx-mch.o
KDIR ?= $(HOME)/mt6785-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules

gen:
	python3 mch-port.py $(KDIR)/drivers/i2c/busses/i2c-mt65xx.c i2c-mt65xx-mch.c

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
