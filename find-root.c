/*
 * Copyright (C) 2011 Red Hat.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <zlib.h>
#include "kerncompat.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "list.h"
#include "version.h"
#include "volumes.h"
#include "utils.h"
#include "crc32c.h"

static int verbose = 0;
static u16 csum_size = 0;

static void usage()
{
	fprintf(stderr, "Usage: find-roots [-v] <device>\n");
}

int csum_block(void *buf, u32 len)
{
	char *result;
	u32 crc = ~(u32)0;
	int ret = 0;

	result = malloc(csum_size * sizeof(char));
	if (!result) {
		fprintf(stderr, "No memory\n");
		return 1;
	}

	len -= BTRFS_CSUM_SIZE;
	crc = crc32c(crc, buf + BTRFS_CSUM_SIZE, len);
	btrfs_csum_final(crc, result);

	if (memcmp(buf, result, csum_size))
		ret = 1;
	free(result);
	return ret;
}

static int __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			u32 stripesize, struct btrfs_root *root,
			struct btrfs_fs_info *fs_info, u64 objectid)
{
	root->node = NULL;
	root->commit_root = NULL;
	root->sectorsize = sectorsize;
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->stripesize = stripesize;
	root->ref_cows = 0;
	root->track_dirty = 0;

	root->fs_info = fs_info;
	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_inode = 0;
	root->last_inode_alloc = 0;

	INIT_LIST_HEAD(&root->dirty_list);
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	root->root_key.objectid = objectid;
	return 0;
}

static int close_all_devices(struct btrfs_fs_info *fs_info)
{
	struct list_head *list;
	struct list_head *next;
	struct btrfs_device *device;

	return 0;

	list = &fs_info->fs_devices->devices;
	list_for_each(next, list) {
		device = list_entry(next, struct btrfs_device, dev_list);
		close(device->fd);
	}
	return 0;
}

static struct btrfs_root *open_ctree_broken(int fd, const char *device)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	u32 stripesize;
	u64 generation;
	struct btrfs_root *tree_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *extent_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *chunk_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *dev_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *csum_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_fs_info *fs_info = malloc(sizeof(*fs_info));
	int ret;
	struct btrfs_super_block *disk_super;
	struct btrfs_fs_devices *fs_devices = NULL;
	u64 total_devs;
	u64 features;

	ret = btrfs_scan_one_device(fd, device, &fs_devices,
				    &total_devs, BTRFS_SUPER_INFO_OFFSET);

	if (ret) {
		fprintf(stderr, "No valid Btrfs found on %s\n", device);
		goto out;
	}

	if (total_devs != 1) {
		ret = btrfs_scan_for_fsid(fs_devices, total_devs, 1);
		if (ret)
			goto out;
	}

	memset(fs_info, 0, sizeof(*fs_info));
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->chunk_root = chunk_root;
	fs_info->dev_root = dev_root;
	fs_info->csum_root = csum_root;

	fs_info->readonly = 1;

	extent_io_tree_init(&fs_info->extent_cache);
	extent_io_tree_init(&fs_info->free_space_cache);
	extent_io_tree_init(&fs_info->block_group_cache);
	extent_io_tree_init(&fs_info->pinned_extents);
	extent_io_tree_init(&fs_info->pending_del);
	extent_io_tree_init(&fs_info->extent_ins);
	cache_tree_init(&fs_info->fs_root_cache);

	cache_tree_init(&fs_info->mapping_tree.cache_tree);

	mutex_init(&fs_info->fs_mutex);
	fs_info->fs_devices = fs_devices;
	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);

	__setup_root(4096, 4096, 4096, 4096, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);

	ret = btrfs_open_devices(fs_devices, O_RDONLY);
	if (ret)
		goto out_cleanup;

	fs_info->super_bytenr = BTRFS_SUPER_INFO_OFFSET;
	disk_super = &fs_info->super_copy;
	ret = btrfs_read_dev_super(fs_devices->latest_bdev,
				   disk_super, BTRFS_SUPER_INFO_OFFSET);
	if (ret) {
		printk("No valid btrfs found\n");
		goto out_devices;
	}

	memcpy(fs_info->fsid, &disk_super->fsid, BTRFS_FSID_SIZE);


	features = btrfs_super_incompat_flags(disk_super) &
		   ~BTRFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		printk("couldn't open because of unsupported "
		       "option features (%Lx).\n", features);
		goto out_devices;
	}

	features = btrfs_super_incompat_flags(disk_super);
	if (!(features & BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF)) {
		features |= BTRFS_FEATURE_INCOMPAT_MIXED_BACKREF;
		btrfs_set_super_incompat_flags(disk_super, features);
	}

	nodesize = btrfs_super_nodesize(disk_super);
	leafsize = btrfs_super_leafsize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = btrfs_super_stripesize(disk_super);
	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;
	tree_root->stripesize = stripesize;

	ret = btrfs_read_sys_array(tree_root);
	if (ret)
		goto out_devices;
	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_chunk_root_level(disk_super));
	generation = btrfs_super_chunk_root_generation(disk_super);

	__setup_root(nodesize, leafsize, sectorsize, stripesize,
		     chunk_root, fs_info, BTRFS_CHUNK_TREE_OBJECTID);

	chunk_root->node = read_tree_block(chunk_root,
					   btrfs_super_chunk_root(disk_super),
					   blocksize, generation);
	if (!chunk_root->node) {
		printk("Couldn't read chunk root\n");
		goto out_devices;
	}

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
	         (unsigned long)btrfs_header_chunk_tree_uuid(chunk_root->node),
		 BTRFS_UUID_SIZE);

	if (!(btrfs_super_flags(disk_super) & BTRFS_SUPER_FLAG_METADUMP)) {
		ret = btrfs_read_chunk_tree(chunk_root);
		if (ret)
			goto out_chunk;
	}

	return fs_info->chunk_root;
out_chunk:
	free_extent_buffer(fs_info->chunk_root->node);
out_devices:
	close_all_devices(fs_info);
out_cleanup:
	extent_io_tree_cleanup(&fs_info->extent_cache);
	extent_io_tree_cleanup(&fs_info->free_space_cache);
	extent_io_tree_cleanup(&fs_info->block_group_cache);
	extent_io_tree_cleanup(&fs_info->pinned_extents);
	extent_io_tree_cleanup(&fs_info->pending_del);
	extent_io_tree_cleanup(&fs_info->extent_ins);
out:
	free(tree_root);
	free(extent_root);
	free(chunk_root);
	free(dev_root);
	free(csum_root);
	free(fs_info);
	return NULL;
}

static int search_iobuf(struct btrfs_root *root, void *iobuf,
			size_t iobuf_size, off_t offset)
{
	u64 gen = btrfs_super_generation(&root->fs_info->super_copy);
	u64 objectid = BTRFS_ROOT_TREE_OBJECTID;
	u32 size = btrfs_super_nodesize(&root->fs_info->super_copy);
	u8 level = root->fs_info->super_copy.root_level;
	size_t block_off = 0;

	while (block_off < iobuf_size) {
		void *block = iobuf + block_off;
		struct btrfs_header *header = block;
		u64 h_byte, h_level, h_gen, h_owner;

		h_byte = le64_to_cpu(header->bytenr);
		h_owner = le64_to_cpu(header->owner);
		h_level = header->level;
		h_gen = le64_to_cpu(header->generation);

		if (h_owner != objectid)
			goto next;
		if (h_byte != (offset + block_off))
			goto next;
		if (h_level != level)
			goto next;
		if (csum_block(block, size)) {
			fprintf(stderr, "Well block %Lu seems good, "
				"but the csum doesn't match\n",
				h_byte);
			goto next;
		}
		if (h_gen != gen) {
			fprintf(stderr, "Well block %Lu seems great, "
				"but generation doesn't match, "
				"have=%Lu, want=%Lu\n", h_byte, h_gen,
				gen);
			goto next;
		}
		printf("Found tree root at %Lu\n", h_byte);
		return 0;
next:
		block_off += size;
	}

	return 1;
}

static int find_root(struct btrfs_root *root)
{
	char *iobuf;
	struct btrfs_multi_bio *multi = NULL;
	struct btrfs_device *device;
	size_t iobuf_size = 4096;
	off_t offset = 0;
	off_t bytenr;
	ssize_t done;
	int fd;
	int err;
	int ret = 1;

	printf("Super think's the tree root is at %Lu, chunk root %Lu\n",
	       btrfs_super_root(&root->fs_info->super_copy),
	       btrfs_super_chunk_root(&root->fs_info->super_copy));

	iobuf = malloc(iobuf_size);
	if (!iobuf) {
		fprintf(stderr, "No memory\n");
		return 1;
	}

	while (1) {
		u64 map_length = iobuf_size;

		if (offset > btrfs_super_root(&root->fs_info->super_copy))
			break;

		err = btrfs_map_block(&root->fs_info->mapping_tree, READ,
				      offset, &map_length, &multi, 0);
		if (err) {
			fprintf(stderr, "Failed to map block %zu, %d\n",
				offset, err);
			break;
		}
		device = multi->stripes[0].dev;
		fd = device->fd;
		bytenr = multi->stripes[0].physical;
		kfree(multi);

		done = pread64(fd, iobuf, iobuf_size, bytenr);
		if (done < 0) {
			fprintf(stderr, "Failed to do read, %s\n",
				strerror(errno));
			break;
		}
		BUG_ON(done != iobuf_size);

		if (!search_iobuf(root, iobuf, done, offset)) {
			ret = 0;
			break;
		}
		offset += done;
	}
	free(iobuf);
	return ret;
}

int main(int argc, char **argv)
{
	struct btrfs_root *root;
	int dev_fd;
	int opt;
	int ret;

	while ((opt = getopt(argc, argv, "v")) != -1) {
		switch(opt) {
			case 'v':
				verbose++;
				break;
			default:
				usage();
				exit(1);
		}
	}

	if (optind >= argc) {
		usage();
		exit(1);
	}

	dev_fd = open(argv[optind], O_RDONLY);
	if (dev_fd < 0) {
		fprintf(stderr, "Failed to open device %s\n", argv[optind]);
		exit(1);
	}

	root = open_ctree_broken(dev_fd, argv[optind]);
	close(dev_fd);
	if (!root)
		exit(1);

	printf("Max xattr size is %d\n", BTRFS_MAX_XATTR_SIZE(root));
	csum_size = btrfs_super_csum_size(&root->fs_info->super_copy);
	ret = find_root(root);
	close_ctree(root);
	return ret;
}