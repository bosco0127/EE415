#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include "filesys/off_t.h"
#include "filesys/inode.h"
#include "devices/block.h"
#include "threads/synch.h"

#define BUFFER_CACHE_ENTRY_NB 64
#define SECTOR_SIZE 512

struct buffer_head {
    bool dirty;
    bool is_used;
    block_sector_t sector;
    bool clock_bit;
    struct lock buffer_lock;
    void *data;
};

bool bc_read (block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs);
bool bc_write (block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs);
struct buffer_head *bc_lookup (block_sector_t sector);
struct buffer_head *bc_select_victim (void);
void bc_flush_entry (struct buffer_head *p_flush_entry);
void bc_flush_all_entries (void);
void bc_init (void);
void bc_term (void);

#endif
