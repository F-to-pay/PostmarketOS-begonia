set -e
W=/mnt/c/Users/1342c/workspace/pmos-begonia/neutrino
S="/mnt/c/Users/1342c/workspace/begonia-twrp-boot-source"
rm -rf /tmp/nt && mkdir -p /tmp/nt && cd /tmp/nt
cp "$S/[0923][Proton][R-OSS][806Mhz][Stable][Begonia]4.14.247-Neutrino-Stock-2a02d61de7d5.zip" stock.zip
unzip -o -q stock.zip -d stock
ls -la stock | head -20; find stock -type f | head -30
