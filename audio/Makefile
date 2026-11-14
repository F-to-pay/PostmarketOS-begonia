# Out-of-tree build of MT6785 audio (AFE + MT6359 + machine) for the phone kernel 6.16.4.
# Usage: see build.sh (WSL). External paths: KDIR = prepared kernel tree.
obj-m += common/ mt6785/ codecs/
# build the ACCDET driver as a module: otherwise the mt6359-accdet.h header substitutes an empty stub
subdir-ccflags-y += -DCONFIG_SND_SOC_MT6359_ACCDET_MODULE=1

KDIR ?= $(HOME)/mt6785-linux

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 KBUILD_MODPOST_WARN=1 LOCALVERSION= modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 LLVM=1 clean
