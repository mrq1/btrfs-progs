/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#ifndef __BTRFS__
#define __BTRFS__

#include "list.h"
#include "kerncompat.h"
#include "radix-tree.h"
#include "extent-cache.h"
#include "extent_map.h"

struct btrfs_root;
struct btrfs_trans_handle;
#define BTRFS_MAGIC "_B3RfS_M"

#define BTRFS_MAX_LEVEL 8
#define BTRFS_ROOT_TREE_OBJECTID 1ULL
#define BTRFS_EXTENT_TREE_OBJECTID 2ULL
#define BTRFS_FS_TREE_OBJECTID 3ULL
#define BTRFS_ROOT_TREE_DIR_OBJECTID 4ULL
#define BTRFS_FIRST_FREE_OBJECTID 256ULL

/*
 * we can actually store much bigger names, but lets not confuse the rest
 * of linux
 */
#define BTRFS_NAME_LEN 255

/* 32 bytes in various csum fields */
#define BTRFS_CSUM_SIZE 32
/* four bytes for CRC32 */
#define BTRFS_CRC32_SIZE 4
#define BTRFS_EMPTY_DIR_SIZE 0

#define BTRFS_FT_UNKNOWN	0
#define BTRFS_FT_REG_FILE	1
#define BTRFS_FT_DIR		2
#define BTRFS_FT_CHRDEV		3
#define BTRFS_FT_BLKDEV		4
#define BTRFS_FT_FIFO		5
#define BTRFS_FT_SOCK		6
#define BTRFS_FT_SYMLINK	7
#define BTRFS_FT_XATTR		8
#define BTRFS_FT_MAX		9

/*
 * the key defines the order in the tree, and so it also defines (optimal)
 * block layout.  objectid corresonds to the inode number.  The flags
 * tells us things about the object, and is a kind of stream selector.
 * so for a given inode, keys with flags of 1 might refer to the inode
 * data, flags of 2 may point to file data in the btree and flags == 3
 * may point to extents.
 *
 * offset is the starting byte offset for this key in the stream.
 *
 * btrfs_disk_key is in disk byte order.  struct btrfs_key is always
 * in cpu native order.  Otherwise they are identical and their sizes
 * should be the same (ie both packed)
 */
struct btrfs_disk_key {
	__le64 objectid;
	u8 type;
	__le64 offset;
} __attribute__ ((__packed__));

struct btrfs_key {
	u64 objectid;
	u8 type;
	u64 offset;
} __attribute__ ((__packed__));

#define BTRFS_FSID_SIZE 16
/*
 * every tree block (leaf or node) starts with this header.
 */
struct btrfs_header {
	u8 csum[BTRFS_CSUM_SIZE];
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */
	__le64 bytenr; /* which block this node is supposed to live in */
	__le64 generation;
	__le64 owner;
	__le32 nritems;
	__le16 flags;
	u8 level;
} __attribute__ ((__packed__));

#define BTRFS_NODEPTRS_PER_BLOCK(r) (((r)->nodesize - \
			        sizeof(struct btrfs_header)) / \
			        sizeof(struct btrfs_key_ptr))
#define __BTRFS_LEAF_DATA_SIZE(bs) ((bs) - sizeof(struct btrfs_header))
#define BTRFS_LEAF_DATA_SIZE(r) (__BTRFS_LEAF_DATA_SIZE(r->leafsize))
#define BTRFS_MAX_INLINE_DATA_SIZE(r) (BTRFS_LEAF_DATA_SIZE(r) - \
					sizeof(struct btrfs_item) - \
					sizeof(struct btrfs_file_extent_item))
/*
 * the super block basically lists the main trees of the FS
 * it currently lacks any block count etc etc
 */
struct btrfs_super_block {
	u8 csum[BTRFS_CSUM_SIZE];
	/* the first 3 fields must match struct btrfs_header */
	u8 fsid[BTRFS_FSID_SIZE];    /* FS specific uuid */
	__le64 bytenr; /* this block number */
	__le64 magic;
	__le64 generation;
	__le64 root;
	__le64 total_bytes;
	__le64 bytes_used;
	__le64 root_dir_objectid;
	__le32 sectorsize;
	__le32 nodesize;
	__le32 leafsize;
	__le32 stripesize;
	u8 root_level;
} __attribute__ ((__packed__));

/*
 * A leaf is full of items. offset and size tell us where to find
 * the item in the leaf (relative to the start of the data area)
 */
struct btrfs_item {
	struct btrfs_disk_key key;
	__le32 offset;
	__le32 size;
} __attribute__ ((__packed__));

/*
 * leaves have an item area and a data area:
 * [item0, item1....itemN] [free space] [dataN...data1, data0]
 *
 * The data is separate from the items to get the keys closer together
 * during searches.
 */
struct btrfs_leaf {
	struct btrfs_header header;
	struct btrfs_item items[];
} __attribute__ ((__packed__));

/*
 * all non-leaf blocks are nodes, they hold only keys and pointers to
 * other blocks
 */
struct btrfs_key_ptr {
	struct btrfs_disk_key key;
	__le64 blockptr;
	__le64 generation;
} __attribute__ ((__packed__));

struct btrfs_node {
	struct btrfs_header header;
	struct btrfs_key_ptr ptrs[];
} __attribute__ ((__packed__));

/*
 * btrfs_paths remember the path taken from the root down to the leaf.
 * level 0 is always the leaf, and nodes[1...BTRFS_MAX_LEVEL] will point
 * to any other levels that are present.
 *
 * The slots array records the index of the item or block pointer
 * used while walking the tree.
 */
struct btrfs_path {
	struct extent_buffer *nodes[BTRFS_MAX_LEVEL];
	int slots[BTRFS_MAX_LEVEL];
	int reada;
	int lowest_level;
};

/*
 * items in the extent btree are used to record the objectid of the
 * owner of the block and the number of references
 */
struct btrfs_extent_item {
	__le32 refs;
} __attribute__ ((__packed__));

struct btrfs_extent_ref {
	__le64 root;
	__le64 generation;
	__le64 objectid;
	__le64 offset;
} __attribute__ ((__packed__));

struct btrfs_inode_ref {
	__le16 name_len;
	/* name goes here */
} __attribute__ ((__packed__));

struct btrfs_inode_timespec {
	__le64 sec;
	__le32 nsec;
} __attribute__ ((__packed__));

/*
 * there is no padding here on purpose.  If you want to extent the inode,
 * make a new item type
 */
struct btrfs_inode_item {
	__le64 generation;
	__le64 size;
	__le64 nblocks;
	__le64 block_group;
	__le32 nlink;
	__le32 uid;
	__le32 gid;
	__le32 mode;
	__le32 rdev;
	__le16 flags;
	__le16 compat_flags;
	struct btrfs_inode_timespec atime;
	struct btrfs_inode_timespec ctime;
	struct btrfs_inode_timespec mtime;
	struct btrfs_inode_timespec otime;
} __attribute__ ((__packed__));

struct btrfs_dir_item {
	struct btrfs_disk_key location;
	__le16 data_len;
	__le16 name_len;
	u8 type;
} __attribute__ ((__packed__));

struct btrfs_root_item {
	struct btrfs_inode_item inode;
	__le64 root_dirid;
	__le64 bytenr;
	__le64 byte_limit;
	__le64 bytes_used;
	__le32 flags;
	__le32 refs;
	struct btrfs_disk_key drop_progress;
	u8 drop_level;
	u8 level;
} __attribute__ ((__packed__));

#define BTRFS_FILE_EXTENT_REG 0
#define BTRFS_FILE_EXTENT_INLINE 1

struct btrfs_file_extent_item {
	__le64 generation;
	u8 type;
	/*
	 * disk space consumed by the extent, checksum blocks are included
	 * in these numbers
	 */
	__le64 disk_bytenr;
	__le64 disk_num_bytes;
	/*
	 * the logical offset in file blocks (no csums)
	 * this extent record is for.  This allows a file extent to point
	 * into the middle of an existing extent on disk, sharing it
	 * between two snapshots (useful if some bytes in the middle of the
	 * extent have changed
	 */
	__le64 offset;
	/*
	 * the logical number of file blocks (no csums included)
	 */
	__le64 num_bytes;
} __attribute__ ((__packed__));

struct btrfs_csum_item {
	u8 csum;
} __attribute__ ((__packed__));

/* tag for the radix tree of block groups in ram */
#define BTRFS_BLOCK_GROUP_SIZE (256 * 1024 * 1024)

#define BTRFS_BLOCK_GROUP_DATA 1
#define BTRFS_BLOCK_GROUP_MIXED 2

struct btrfs_block_group_item {
	__le64 used;
	u8 flags;
} __attribute__ ((__packed__));

struct btrfs_block_group_cache {
	struct cache_extent cache;
	struct btrfs_key key;
	struct btrfs_block_group_item item;
	int data;
	int cached;
	u64 pinned;
};
struct btrfs_extent_ops {
       int (*alloc_extent)(struct btrfs_root *root, u64 num_bytes,
		           u64 hint_byte, struct btrfs_key *ins);
       int (*free_extent)(struct btrfs_root *root, u64 bytenr,
		          u64 num_bytes);
};

struct btrfs_fs_info {
	u8 fsid[BTRFS_FSID_SIZE];
	struct btrfs_root *fs_root;
	struct btrfs_root *extent_root;
	struct btrfs_root *tree_root;

	struct extent_map_tree extent_cache;
	struct extent_map_tree free_space_cache;
	struct extent_map_tree block_group_cache;
	struct extent_map_tree pending_tree;
	struct extent_map_tree pinned_extents;
	struct extent_map_tree del_pending;
	struct extent_map_tree pending_del;
	struct extent_map_tree extent_ins;

	u64 generation;
	u64 last_trans_committed;
	struct btrfs_trans_handle *running_transaction;
	struct btrfs_super_block super_copy;
	struct extent_buffer *sb_buffer;
	struct mutex fs_mutex;
	int fp;
	u64 total_pinned;

	struct btrfs_extent_ops *extent_ops;
	void *priv_data;
};
/*
 * in ram representation of the tree.  extent_root is used for all allocations
 * and for the extent tree extent_root root.
 */
struct btrfs_root {
	struct extent_buffer *node;
	struct extent_buffer *commit_root;
	struct btrfs_root_item root_item;
	struct btrfs_key root_key;
	struct btrfs_fs_info *fs_info;
	u64 objectid;
	u64 last_trans;

	/* data allocations are done in sectorsize units */
	u32 sectorsize;

	/* node allocations are done in nodesize units */
	u32 nodesize;

	/* leaf allocations are done in leafsize units */
	u32 leafsize;

	/* leaf allocations are done in leafsize units */
	u32 stripesize;

	int ref_cows;

	u32 type;
	u64 highest_inode;
	u64 last_inode_alloc;
};

/*
 * inode items have the data typically returned from stat and store other
 * info about object characteristics.  There is one for every file and dir in
 * the FS
 */
#define BTRFS_INODE_ITEM_KEY		1
#define BTRFS_INODE_REF_KEY		2
#define BTRFS_XATTR_ITEM_KEY		8

/* reserve 3-15 close to the inode for later flexibility */

/*
 * dir items are the name -> inode pointers in a directory.  There is one
 * for every name in a directory.
 */
#define BTRFS_DIR_ITEM_KEY	16
#define BTRFS_DIR_INDEX_KEY	17
/*
 * extent data is for file data
 */
#define BTRFS_EXTENT_DATA_KEY	18
/*
 * csum items have the checksums for data in the extents
 */
#define BTRFS_CSUM_ITEM_KEY	19

/* reserve 20-31 for other file stuff */

/*
 * root items point to tree roots.  There are typically in the root
 * tree used by the super block to find all the other trees
 */
#define BTRFS_ROOT_ITEM_KEY	32
/*
 * extent items are in the extent map tree.  These record which blocks
 * are used, and how many references there are to each block
 */
#define BTRFS_EXTENT_ITEM_KEY	33
#define BTRFS_EXTENT_REF_KEY	34

/*
 * block groups give us hints into the extent allocation trees.  Which
 * blocks are free etc etc
 */
#define BTRFS_BLOCK_GROUP_ITEM_KEY 50

/*
 * string items are for debugging.  They just store a short string of
 * data in the FS
 */
#define BTRFS_STRING_ITEM_KEY	253
/*
 * Inode flags
 */
#define BTRFS_INODE_NODATASUM		(1 << 0)
#define BTRFS_INODE_NODATACOW		(1 << 1)
#define BTRFS_INODE_READONLY		(1 << 2)

#define read_eb_member(eb, ptr, type, member, result) (			\
	read_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			   sizeof(((type *)0)->member)))

#define write_eb_member(eb, ptr, type, member, result) (		\
	write_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			   sizeof(((type *)0)->member)))

#define BTRFS_SETGET_HEADER_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(struct extent_buffer *eb)		\
{									\
	struct btrfs_header *h = (struct btrfs_header *)eb->data;	\
	return le##bits##_to_cpu(h->member);				\
}									\
static inline void btrfs_set_##name(struct extent_buffer *eb,		\
				    u##bits val)			\
{									\
	struct btrfs_header *h = (struct btrfs_header *)eb->data;	\
	h->member = cpu_to_le##bits(val);				\
}

#define BTRFS_SETGET_FUNCS(name, type, member, bits)			\
static inline u##bits btrfs_##name(struct extent_buffer *eb,		\
				   type *s)				\
{									\
	unsigned long offset = (unsigned long)s +			\
				offsetof(type, member);			\
	__le##bits *tmp = (__le##bits *)(eb->data + offset);		\
	return le##bits##_to_cpu(*tmp);					\
}									\
static inline void btrfs_set_##name(struct extent_buffer *eb,		\
				    type *s, u##bits val)		\
{									\
	unsigned long offset = (unsigned long)s +			\
				offsetof(type, member);			\
	__le##bits *tmp = (__le##bits *)(eb->data + offset);		\
	*tmp = cpu_to_le##bits(val);					\
}

#define BTRFS_SETGET_STACK_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(type *s)				\
{									\
	return le##bits##_to_cpu(s->member);				\
}									\
static inline void btrfs_set_##name(type *s, u##bits val)		\
{									\
	s->member = cpu_to_le##bits(val);				\
}

/* struct btrfs_block_group_item */
BTRFS_SETGET_STACK_FUNCS(block_group_used, struct btrfs_block_group_item,
			 used, 64);
BTRFS_SETGET_FUNCS(disk_block_group_used, struct btrfs_block_group_item,
			 used, 64);

/* struct btrfs_inode_ref */
BTRFS_SETGET_FUNCS(inode_ref_name_len, struct btrfs_inode_ref, name_len, 16);

/* struct btrfs_inode_item */
BTRFS_SETGET_FUNCS(inode_generation, struct btrfs_inode_item, generation, 64);
BTRFS_SETGET_FUNCS(inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_FUNCS(inode_nblocks, struct btrfs_inode_item, nblocks, 64);
BTRFS_SETGET_FUNCS(inode_block_group, struct btrfs_inode_item, block_group, 64);
BTRFS_SETGET_FUNCS(inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_FUNCS(inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_FUNCS(inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_FUNCS(inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_FUNCS(inode_rdev, struct btrfs_inode_item, rdev, 32);
BTRFS_SETGET_FUNCS(inode_flags, struct btrfs_inode_item, flags, 16);
BTRFS_SETGET_FUNCS(inode_compat_flags, struct btrfs_inode_item,
		   compat_flags, 16);

BTRFS_SETGET_STACK_FUNCS(stack_inode_generation,
			 struct btrfs_inode_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_size,
			 struct btrfs_inode_item, size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nblocks,
			 struct btrfs_inode_item, nblocks, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_block_group,
			 struct btrfs_inode_item, block_group, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nlink,
			 struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_uid,
			 struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_gid,
			 struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_mode,
			 struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_rdev,
			 struct btrfs_inode_item, rdev, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_flags,
			 struct btrfs_inode_item, flags, 16);
BTRFS_SETGET_STACK_FUNCS(stack_inode_compat_flags,
			 struct btrfs_inode_item, compat_flags, 16);

static inline struct btrfs_inode_timespec *
btrfs_inode_atime(struct btrfs_inode_item *inode_item)
{
	unsigned long ptr = (unsigned long)inode_item;
	ptr += offsetof(struct btrfs_inode_item, atime);
	return (struct btrfs_inode_timespec *)ptr;
}

static inline struct btrfs_inode_timespec *
btrfs_inode_mtime(struct btrfs_inode_item *inode_item)
{
	unsigned long ptr = (unsigned long)inode_item;
	ptr += offsetof(struct btrfs_inode_item, mtime);
	return (struct btrfs_inode_timespec *)ptr;
}

static inline struct btrfs_inode_timespec *
btrfs_inode_ctime(struct btrfs_inode_item *inode_item)
{
	unsigned long ptr = (unsigned long)inode_item;
	ptr += offsetof(struct btrfs_inode_item, ctime);
	return (struct btrfs_inode_timespec *)ptr;
}

static inline struct btrfs_inode_timespec *
btrfs_inode_otime(struct btrfs_inode_item *inode_item)
{
	unsigned long ptr = (unsigned long)inode_item;
	ptr += offsetof(struct btrfs_inode_item, otime);
	return (struct btrfs_inode_timespec *)ptr;
}

BTRFS_SETGET_FUNCS(timespec_sec, struct btrfs_inode_timespec, sec, 64);
BTRFS_SETGET_FUNCS(timespec_nsec, struct btrfs_inode_timespec, nsec, 32);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_sec, struct btrfs_inode_timespec,
			 sec, 64);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_nsec, struct btrfs_inode_timespec,
			 nsec, 32);

/* struct btrfs_extent_item */
BTRFS_SETGET_FUNCS(extent_refs, struct btrfs_extent_item, refs, 32);

/* struct btrfs_extent_ref */
BTRFS_SETGET_FUNCS(ref_root, struct btrfs_extent_ref, root, 64);
BTRFS_SETGET_FUNCS(ref_generation, struct btrfs_extent_ref, generation, 64);
BTRFS_SETGET_FUNCS(ref_objectid, struct btrfs_extent_ref, objectid, 64);
BTRFS_SETGET_FUNCS(ref_offset, struct btrfs_extent_ref, offset, 64);

BTRFS_SETGET_STACK_FUNCS(stack_ref_root, struct btrfs_extent_ref, root, 64);
BTRFS_SETGET_STACK_FUNCS(stack_ref_generation, struct btrfs_extent_ref,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_ref_objectid, struct btrfs_extent_ref,
			 objectid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_ref_offset, struct btrfs_extent_ref, offset, 64);

BTRFS_SETGET_STACK_FUNCS(stack_extent_refs, struct btrfs_extent_item,
			 refs, 32);

/* struct btrfs_node */
BTRFS_SETGET_FUNCS(key_blockptr, struct btrfs_key_ptr, blockptr, 64);
BTRFS_SETGET_FUNCS(key_generation, struct btrfs_key_ptr, generation, 64);

static inline u64 btrfs_node_blockptr(struct extent_buffer *eb, int nr)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_blockptr(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_blockptr(struct extent_buffer *eb,
					   int nr, u64 val)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_blockptr(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline u64 btrfs_node_ptr_generation(struct extent_buffer *eb, int nr)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_generation(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_ptr_generation(struct extent_buffer *eb,
						 int nr, u64 val)
{
	unsigned long ptr;
	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_generation(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline unsigned long btrfs_node_key_ptr_offset(int nr)
{
	return offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
}

static inline void btrfs_node_key(struct extent_buffer *eb,
				  struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr;
	ptr = btrfs_node_key_ptr_offset(nr);
	read_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}

static inline void btrfs_set_node_key(struct extent_buffer *eb,
				      struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr;
	ptr = btrfs_node_key_ptr_offset(nr);
	write_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		       struct btrfs_key_ptr, key, disk_key);
}

/* struct btrfs_item */
BTRFS_SETGET_FUNCS(item_offset, struct btrfs_item, offset, 32);
BTRFS_SETGET_FUNCS(item_size, struct btrfs_item, size, 32);

static inline unsigned long btrfs_item_nr_offset(int nr)
{
	return offsetof(struct btrfs_leaf, items) +
		sizeof(struct btrfs_item) * nr;
}

static inline struct btrfs_item *btrfs_item_nr(struct extent_buffer *eb,
					       int nr)
{
	return (struct btrfs_item *)btrfs_item_nr_offset(nr);
}

static inline u32 btrfs_item_end(struct extent_buffer *eb,
				 struct btrfs_item *item)
{
	return btrfs_item_offset(eb, item) + btrfs_item_size(eb, item);
}

static inline u32 btrfs_item_end_nr(struct extent_buffer *eb, int nr)
{
	return btrfs_item_end(eb, btrfs_item_nr(eb, nr));
}

static inline u32 btrfs_item_offset_nr(struct extent_buffer *eb, int nr)
{
	return btrfs_item_offset(eb, btrfs_item_nr(eb, nr));
}

static inline u32 btrfs_item_size_nr(struct extent_buffer *eb, int nr)
{
	return btrfs_item_size(eb, btrfs_item_nr(eb, nr));
}

static inline void btrfs_item_key(struct extent_buffer *eb,
			   struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(eb, nr);
	read_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

static inline void btrfs_set_item_key(struct extent_buffer *eb,
			       struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(eb, nr);
	write_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

/* struct btrfs_dir_item */
BTRFS_SETGET_FUNCS(dir_data_len, struct btrfs_dir_item, data_len, 16);
BTRFS_SETGET_FUNCS(dir_type, struct btrfs_dir_item, type, 8);
BTRFS_SETGET_FUNCS(dir_name_len, struct btrfs_dir_item, name_len, 16);

static inline void btrfs_dir_item_key(struct extent_buffer *eb,
				      struct btrfs_dir_item *item,
				      struct btrfs_disk_key *key)
{
	read_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

static inline void btrfs_set_dir_item_key(struct extent_buffer *eb,
					  struct btrfs_dir_item *item,
					  struct btrfs_disk_key *key)
{
	write_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

/* struct btrfs_disk_key */
BTRFS_SETGET_STACK_FUNCS(disk_key_objectid, struct btrfs_disk_key,
			 objectid, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_offset, struct btrfs_disk_key, offset, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_type, struct btrfs_disk_key, type, 8);

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu,
					 struct btrfs_disk_key *disk)
{
	cpu->offset = le64_to_cpu(disk->offset);
	cpu->type = disk->type;
	cpu->objectid = le64_to_cpu(disk->objectid);
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk,
					 struct btrfs_key *cpu)
{
	disk->offset = cpu_to_le64(cpu->offset);
	disk->type = cpu->type;
	disk->objectid = cpu_to_le64(cpu->objectid);
}

static inline void btrfs_node_key_to_cpu(struct extent_buffer *eb,
				  struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;
	btrfs_node_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_item_key_to_cpu(struct extent_buffer *eb,
				  struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;
	btrfs_item_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_dir_item_key_to_cpu(struct extent_buffer *eb,
				      struct btrfs_dir_item *item,
				      struct btrfs_key *key)
{
	struct btrfs_disk_key disk_key;
	btrfs_dir_item_key(eb, item, &disk_key);
	btrfs_disk_key_to_cpu(key, &disk_key);
}


static inline u8 btrfs_key_type(struct btrfs_key *key)
{
	return key->type;
}

static inline void btrfs_set_key_type(struct btrfs_key *key, u8 val)
{
	key->type = val;
}

/* struct btrfs_header */
BTRFS_SETGET_HEADER_FUNCS(header_bytenr, struct btrfs_header, bytenr, 64);
BTRFS_SETGET_HEADER_FUNCS(header_generation, struct btrfs_header,
			  generation, 64);
BTRFS_SETGET_HEADER_FUNCS(header_owner, struct btrfs_header, owner, 64);
BTRFS_SETGET_HEADER_FUNCS(header_nritems, struct btrfs_header, nritems, 32);
BTRFS_SETGET_HEADER_FUNCS(header_flags, struct btrfs_header, flags, 16);
BTRFS_SETGET_HEADER_FUNCS(header_level, struct btrfs_header, level, 8);

static inline u8 *btrfs_header_fsid(struct extent_buffer *eb)
{
	unsigned long ptr = offsetof(struct btrfs_header, fsid);
	return (u8 *)ptr;
}

static inline u8 *btrfs_super_fsid(struct extent_buffer *eb)
{
	unsigned long ptr = offsetof(struct btrfs_super_block, fsid);
	return (u8 *)ptr;
}

static inline u8 *btrfs_header_csum(struct extent_buffer *eb)
{
	unsigned long ptr = offsetof(struct btrfs_header, csum);
	return (u8 *)ptr;
}

static inline struct btrfs_node *btrfs_buffer_node(struct extent_buffer *eb)
{
	return NULL;
}

static inline struct btrfs_leaf *btrfs_buffer_leaf(struct extent_buffer *eb)
{
	return NULL;
}

static inline struct btrfs_header *btrfs_buffer_header(struct extent_buffer *eb)
{
	return NULL;
}

static inline int btrfs_is_leaf(struct extent_buffer *eb)
{
	return (btrfs_header_level(eb) == 0);
}

/* struct btrfs_root_item */
BTRFS_SETGET_FUNCS(disk_root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_FUNCS(disk_root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_FUNCS(disk_root_level, struct btrfs_root_item, level, 8);

BTRFS_SETGET_STACK_FUNCS(root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(root_level, struct btrfs_root_item, level, 8);
BTRFS_SETGET_STACK_FUNCS(root_dirid, struct btrfs_root_item, root_dirid, 64);
BTRFS_SETGET_STACK_FUNCS(root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_STACK_FUNCS(root_flags, struct btrfs_root_item, flags, 32);
BTRFS_SETGET_STACK_FUNCS(root_used, struct btrfs_root_item, bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(root_limit, struct btrfs_root_item, byte_limit, 64);

/* struct btrfs_super_block */
BTRFS_SETGET_STACK_FUNCS(super_bytenr, struct btrfs_super_block, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(super_generation, struct btrfs_super_block,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_root, struct btrfs_super_block, root, 64);
BTRFS_SETGET_STACK_FUNCS(super_root_level, struct btrfs_super_block,
			 root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_total_bytes, struct btrfs_super_block,
			 total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(super_bytes_used, struct btrfs_super_block,
			 bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(super_sectorsize, struct btrfs_super_block,
			 sectorsize, 32);
BTRFS_SETGET_STACK_FUNCS(super_nodesize, struct btrfs_super_block,
			 nodesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_leafsize, struct btrfs_super_block,
			 leafsize, 32);
BTRFS_SETGET_STACK_FUNCS(super_stripesize, struct btrfs_super_block,
			 stripesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_root_dir, struct btrfs_super_block,
			 root_dir_objectid, 64);

static inline unsigned long btrfs_leaf_data(struct extent_buffer *l)
{
	return offsetof(struct btrfs_leaf, items);
}

/* struct btrfs_file_extent_item */
BTRFS_SETGET_FUNCS(file_extent_type, struct btrfs_file_extent_item, type, 8);

static inline unsigned long btrfs_file_extent_inline_start(struct
						   btrfs_file_extent_item *e)
{
	unsigned long offset = (unsigned long)e;
	offset += offsetof(struct btrfs_file_extent_item, disk_bytenr);
	return offset;
}

static inline u32 btrfs_file_extent_calc_inline_size(u32 datasize)
{
	return offsetof(struct btrfs_file_extent_item, disk_bytenr) + datasize;
}

static inline u32 btrfs_file_extent_inline_len(struct extent_buffer *eb,
					       struct btrfs_item *e)
{
	unsigned long offset;
	offset = offsetof(struct btrfs_file_extent_item, disk_bytenr);
	return btrfs_item_size(eb, e) - offset;
}

BTRFS_SETGET_FUNCS(file_extent_disk_bytenr, struct btrfs_file_extent_item,
		   disk_bytenr, 64);
BTRFS_SETGET_FUNCS(file_extent_generation, struct btrfs_file_extent_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(file_extent_disk_num_bytes, struct btrfs_file_extent_item,
		   disk_num_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_offset, struct btrfs_file_extent_item,
		  offset, 64);
BTRFS_SETGET_FUNCS(file_extent_num_bytes, struct btrfs_file_extent_item,
		   num_bytes, 64);

static inline u32 btrfs_level_size(struct btrfs_root *root, int level) {
	if (level == 0)
		return root->leafsize;
	return root->nodesize;
}

/* helper function to cast into the data area of the leaf. */
#define btrfs_item_ptr(leaf, slot, type) \
	((type *)(btrfs_leaf_data(leaf) + \
	btrfs_item_offset_nr(leaf, slot)))

#define btrfs_item_ptr_offset(leaf, slot) \
	((unsigned long)(btrfs_leaf_data(leaf) + \
	btrfs_item_offset_nr(leaf, slot)))

/* extent-tree.c */
u32 btrfs_count_snapshots_in_path(struct btrfs_root *root,
				  struct btrfs_path *count_path,
				  u64 first_extent);
int btrfs_extent_post_op(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root);
int btrfs_copy_pinned(struct btrfs_root *root, struct extent_map_tree *copy);
struct btrfs_block_group_cache *btrfs_lookup_block_group(struct
							 btrfs_fs_info *info,
							 u64 bytenr);
struct btrfs_block_group_cache *btrfs_find_block_group(struct btrfs_root *root,
						 struct btrfs_block_group_cache
						 *hint, u64 search_start,
						 int data, int owner);
int btrfs_inc_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, u64 owner_objectid);
struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root, u32 size,
					    u64 root_objectid,
					    u64 hint, u64 empty_size);
struct extent_buffer *__btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u32 blocksize,
					     u64 root_objectid,
					     u64 ref_generation,
					     u64 first_objectid,
					     int level,
					     u64 hint,
					     u64 empty_size);
int btrfs_grow_extent_tree(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 new_size);
int btrfs_shrink_extent_tree(struct btrfs_root *root, u64 new_size);
int btrfs_insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path, u64 bytenr,
				 u64 root_objectid, u64 ref_generation,
				 u64 owner, u64 owner_offset);
int btrfs_alloc_extent(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       u64 num_bytes, u64 root_objectid, u64 ref_generation,
		       u64 owner, u64 owner_offset,
		       u64 empty_size, u64 hint_byte,
		       u64 search_end, struct btrfs_key *ins, int data);
int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf);
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, u64 bytenr, u64 num_bytes,
		      u64 root_objectid, u64 ref_generation,
		      u64 owner_objectid, u64 owner_offset, int pin);
int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_map_tree *unpin);
int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes,
				u64 root_objectid, u64 ref_generation,
				u64 owner, u64 owner_offset);
int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root);
int btrfs_free_block_groups(struct btrfs_fs_info *info);
int btrfs_read_block_groups(struct btrfs_root *root);
int btrfs_make_block_groups(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
u64 btrfs_hash_extent_ref(u64 root_objectid, u64 ref_generation,
			  u64 owner, u64 owner_offset);
int btrfs_update_block_group(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 bytenr, u64 num,
			     int alloc, int mark_free, int data);
/* ctree.c */
int btrfs_comp_keys(struct btrfs_disk_key *disk, struct btrfs_key *k2);
int btrfs_cow_block(struct btrfs_trans_handle *trans,
		    struct btrfs_root *root, struct extent_buffer *buf,
		    struct extent_buffer *parent, int parent_slot,
		    struct extent_buffer **cow_ret);
int btrfs_copy_root(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      struct extent_buffer *buf,
		      struct extent_buffer **cow_ret, u64 new_root_objectid);
int btrfs_extend_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_path *path, u32 data_size);
int btrfs_truncate_item(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_path *path,
			u32 new_size, int from_end);
int btrfs_search_slot(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_path *p, int
		      ins_len, int cow);
int btrfs_realloc_node(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct extent_buffer *parent,
		       int start_slot, int cache_only, u64 *last_ret,
		       struct btrfs_key *progress);
void btrfs_release_path(struct btrfs_root *root, struct btrfs_path *p);
struct btrfs_path *btrfs_alloc_path(void);
void btrfs_free_path(struct btrfs_path *p);
void btrfs_init_path(struct btrfs_path *p);
int btrfs_del_item(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_path *path);
int btrfs_insert_item(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, void *data, u32 data_size);
int btrfs_insert_empty_item(struct btrfs_trans_handle *trans, struct btrfs_root
			    *root, struct btrfs_path *path, struct btrfs_key
			    *cpu_key, u32 data_size);
int btrfs_next_leaf(struct btrfs_root *root, struct btrfs_path *path);
int btrfs_prev_leaf(struct btrfs_root *root, struct btrfs_path *path);
int btrfs_leaf_free_space(struct btrfs_root *root, struct extent_buffer *leaf);
int btrfs_drop_snapshot(struct btrfs_trans_handle *trans, struct btrfs_root
			*root);


/* root-item.c */
int btrfs_del_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_key *key);
int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item);
int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item);
int btrfs_find_last_root(struct btrfs_root *root, u64 objectid, struct
			 btrfs_root_item *item, struct btrfs_key *key);
int btrfs_find_dead_roots(struct btrfs_root *root, u64 objectid,
			  struct btrfs_root *latest_root);
/* dir-item.c */
int btrfs_insert_dir_item(struct btrfs_trans_handle *trans, struct btrfs_root
			  *root, const char *name, int name_len, u64 dir,
			  struct btrfs_key *location, u8 type);
struct btrfs_dir_item *btrfs_lookup_dir_item(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     struct btrfs_path *path, u64 dir,
					     const char *name, int name_len,
					     int mod);
struct btrfs_dir_item *
btrfs_lookup_dir_index_item(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct btrfs_path *path, u64 dir,
			    u64 objectid, const char *name, int name_len,
			    int mod);
struct btrfs_dir_item *btrfs_match_dir_item_name(struct btrfs_root *root,
			      struct btrfs_path *path,
			      const char *name, int name_len);
int btrfs_delete_one_dir_name(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct btrfs_path *path,
			      struct btrfs_dir_item *di);
int btrfs_insert_xattr_item(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, const char *name,
			    u16 name_len, const void *data, u16 data_len,
			    u64 dir);
struct btrfs_dir_item *btrfs_lookup_xattr(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path, u64 dir,
					  const char *name, u16 name_len,
					  int mod);
/* inode-map.c */
int btrfs_find_free_objectid(struct btrfs_trans_handle *trans,
			     struct btrfs_root *fs_root,
			     u64 dirid, u64 *objectid);
int btrfs_find_highest_inode(struct btrfs_root *fs_root, u64 *objectid);

/* inode-item.c */
int btrfs_insert_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid);
int btrfs_del_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid);
int btrfs_insert_empty_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid);
int btrfs_insert_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, u64 objectid, struct btrfs_inode_item
		       *inode_item);
int btrfs_lookup_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, struct btrfs_path *path,
		       struct btrfs_key *location, int mod);

/* file-item.c */
int btrfs_insert_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     u64 objectid, u64 pos, u64 offset,
			     u64 disk_num_bytes,
			     u64 num_bytes);
int btrfs_insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, u64 objectid,
				u64 offset, char *buffer, size_t size);
int btrfs_lookup_file_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid,
			     u64 bytenr, int mod);
int btrfs_csum_file_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_inode_item *inode,
			  u64 objectid, u64 offset,
			  char *data, size_t len);
struct btrfs_csum_item *btrfs_lookup_csum(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 objectid, u64 offset,
					  int cow);
int btrfs_csum_truncate(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, struct btrfs_path *path,
			u64 isize);
#endif
