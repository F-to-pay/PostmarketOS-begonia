# Out-of-tree build against the prepared mt6785-mainline tree (6.16.4).
# The tree is prepared by wifi/build.sh (phone config + modules_prepare). KDIR=$HOME/mt6785-linux by default.
obj-m += consys-bringup.o
ccflags-y += -I$(src)/gen4m-port/shim/include
KDIR ?= $(HOME)/mt6785-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
