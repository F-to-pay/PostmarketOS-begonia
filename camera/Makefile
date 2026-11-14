# Out-of-tree build of cam-probe against the prepared 6.16.4 tree (see ../wifi/build.sh).
obj-m += cam-probe.o
KDIR ?= $(HOME)/mt6785-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
