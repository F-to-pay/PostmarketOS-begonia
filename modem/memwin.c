// SPDX-License-Identifier: GPL-2.0
/*
 * memwin: a READ-ONLY window onto physical memory (for reconnaissance: what LK left behind).
 * insmod memwin.ko phys=0xbff60000 size=0x10000  ->  /sys/kernel/debug/memwin/blob (read with dd/xxd).
 * Writes nothing. A region closed by the EMI MPU for the AP may cause a hardware error on read.
 */
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>

static unsigned long long phys;
module_param(phys, ullong, 0444);
static unsigned long size = 0x1000;
module_param(size, ulong, 0444);

static struct dentry *dir;
static struct debugfs_blob_wrapper blob;
static void *map;

static int __init memwin_init(void)
{
	map = memremap(phys, size, MEMREMAP_WB);
	if (!map)
		return -ENOMEM;
	blob.data = map;
	blob.size = size;
	dir = debugfs_create_dir("memwin", NULL);
	debugfs_create_blob("blob", 0400, dir, &blob);
	pr_info("memwin: 0x%llx + 0x%lx -> /sys/kernel/debug/memwin/blob\n", phys, size);
	return 0;
}

static void __exit memwin_exit(void)
{
	debugfs_remove_recursive(dir);
	memunmap(map);
}

module_init(memwin_init);
module_exit(memwin_exit);
MODULE_DESCRIPTION("read-only physical memory window (debug)");
MODULE_LICENSE("GPL");
