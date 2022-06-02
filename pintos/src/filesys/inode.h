#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES 128

struct bitmap;

/* In-memory inode. */
struct inode {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;             /* Inode lock. */
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t is_dir;
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
    block_sector_t indirect_block_sec;
    block_sector_t double_indirect_block_sec;
};

enum direct_t {
  NORMAL_DIRECT, // Save disk block number to the inode
  INDIRECT, // Access disk block number through one index block
  DOUBLE_INDIRECT, // Access disk block number through two index block
  OUT_LIMIT // Invalid file offset
};

struct sector_location {
  int directness; // Disk block access method
  int index1; // First index block offset
  int index2; // Second index block offset
};

struct inode_indirect_block {
  block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
};

void inode_init (void);
bool inode_create (block_sector_t, off_t, uint32_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

static bool get_disk_inode (const struct inode *inode, struct inode_disk *inode_disk);
static void locate_byte (off_t pos, struct sector_location *sec_loc);
static inline off_t map_table_offset (int index);
static bool register_sector (struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc);
bool inode_update_file_length (struct inode_disk *inode_disk, off_t start_pos, off_t end_pos);
static void free_inode_sectors (struct inode_disk *inode_disk);
bool inode_is_dir (const struct inode *inode);
#endif /* filesys/inode.h */
