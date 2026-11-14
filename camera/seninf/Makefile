obj-m += seninf-tm.o
KDIR ?= $(HOME)/mt6785-linux
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
