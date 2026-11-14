#!/bin/sh
# Installs the toolchain to build kernel modules right on the phone. Run: sh this_file
set -e
echo "== installing packages (will ask for the sudo password) =="
sudo apk add build-base clang lld llvm make bison flex openssl-dev \
	elfutils-dev perl bc pahole dtc python3 linux-headers
echo "== check =="
for t in clang ld.lld make bison flex pahole dtc bc perl; do
	printf '%-8s ' "$t"; command -v "$t" || echo "MISSING"
done
echo "done"
