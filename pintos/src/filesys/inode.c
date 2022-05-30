#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
//struct inode_disk
//  {
//    block_sector_t start;               /* First data sector. */
//    off_t length;                       /* File size in bytes. */
//    unsigned magic;                     /* Magic number. */
//    uint32_t unused[125];               /* Not used. */
//  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
//struct inode 
//  {
//    struct list_elem elem;              /* Element in inode list. */
//    block_sector_t sector;              /* Sector number of disk location. */
//   int open_cnt;                       /* Number of openers. */
//    bool removed;                       /* True if deleted, false otherwise. */
//    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//    struct inode_disk data;             /* Inode content. */
//  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{
  block_sector_t result_sec;

  if (pos < inode_disk->length) {
    struct inode_indirect_block *ind_block;
    struct inode_indirect_block *second_ind_block;
    struct sector_location sec_loc;
    locate_byte(pos, &sec_loc);

    switch (sec_loc.directness) {
      case NORMAL_DIRECT:
        result_sec = inode_disk->direct_map_table[sec_loc.index1];
        break;

      case INDIRECT:
        ind_block = (struct inode_indirect_block *)malloc(BLOCK_SECTOR_SIZE);
        if (ind_block != NULL) {
          // Read index block1
          bc_read(inode_disk->indirect_block_sec, (void *)ind_block, 0, SECTOR_SIZE, 0);

          // Check disk block number from the index block
          result_sec = ind_block->map_table[sec_loc.index1];
          free(ind_block);
        }
        else {
          result_sec = 0;
        }
        break;

      case DOUBLE_INDIRECT:
        ind_block = (struct inode_indirect_block *)malloc(BLOCK_SECTOR_SIZE);
        if (ind_block == NULL) {
          result_sec = 0;
          break;
        }
        second_ind_block = (struct inode_indirect_block *)malloc(BLOCK_SECTOR_SIZE);
        if (second_ind_block == NULL) {
          result_sec = 0;
          free(ind_block);
          break;
        }
        // Read index block1 & 2 from the disk.
        bc_read(inode_disk->double_indirect_block_sec, (void *)ind_block, 0, SECTOR_SIZE, 0);
        bc_read(ind_block->map_table[sec_loc.index1], (void *)second_ind_block, 0, SECTOR_SIZE, 0);
        result_sec = second_ind_block->map_table[sec_loc.index2];
        free(second_ind_block);
        free(ind_block);
        break;

      default:
        result_sec = 0;
        break;
    }
  }
  else {
    result_sec = 0;
  }
  
  return result_sec;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length /*uint32_t is_dir*/)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      //disk_inode->is_dir = is_dir;
      if (length > 0) {
          if(inode_update_file_length(disk_inode, 0, length) == false) {
            printf("inode_update_file_length failed!\n");
            free(disk_inode);
            return success;
          }
      }
      bc_write(sector, disk_inode, 0, SECTOR_SIZE, 0);
      free (disk_inode);
      success = true;
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->extend_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    lock_acquire(&inode->extend_lock);
    inode->open_cnt++;
    lock_release(&inode->extend_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  int count = 0;
  lock_acquire(&inode->extend_lock);
  count = --inode->open_cnt;
  lock_release(&inode->extend_lock);
  if (count == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct inode_disk inode_disk;
          lock_acquire(&inode->extend_lock);
          get_disk_inode(inode, &inode_disk);
          free_inode_sectors(&inode_disk);
          free_map_release(inode->sector, 1);
          lock_release(&inode->extend_lock);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  struct inode_disk *inode_disk;
  inode_disk = (struct inode_disk *) malloc(BLOCK_SECTOR_SIZE);
  if(inode_disk == NULL) {
    return -1;
  }

  lock_acquire(&inode->extend_lock);
  get_disk_inode(inode, inode_disk);
  
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode_disk, offset);
      //lock_release(&inode->extend_lock);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        //lock_acquire(&inode->extend_lock);
        break;
      }

      /*if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer.
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy into caller's buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }*/

      // Read from the buffer cache first.
      bc_read(sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;

      //lock_acquire(&inode->extend_lock);
    }
  lock_release(&inode->extend_lock);
  free (inode_disk);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  struct inode_disk *disk_inode;

  if (inode->deny_write_cnt)
    return 0;

  disk_inode = (struct inode_disk *) malloc(BLOCK_SECTOR_SIZE);
  if(disk_inode == NULL) {
    return 0;
  }

  lock_acquire(&inode->extend_lock);
  get_disk_inode(inode, disk_inode);

  int old_length = disk_inode->length;
  int write_end = offset + size - 1;
  if(write_end > old_length -1) {
    inode_update_file_length(disk_inode, old_length, write_end + 1);
    disk_inode->length += write_end - old_length + 1;
    // Update disk_inode to disk
    bc_write(inode->sector, (void *)disk_inode, 0, SECTOR_SIZE, 0);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      //lock_release(&inode->extend_lock);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        //lock_acquire(&inode->extend_lock);
        break;
      }

      /*if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Write full sector directly to disk.
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          // We need a bounce buffer.
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          //   we're writing, then we need to read in the sector
          //   first.  Otherwise we start with a sector of all zeros.
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }*/

      // Write to the buffer cache first.
      bc_write(sector_idx, buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;

      //lock_acquire(&inode->extend_lock);
    }
  lock_release(&inode->extend_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk disk_inode;
  get_disk_inode(inode, &disk_inode);
  return disk_inode.length;
}

static bool get_disk_inode (const struct inode *inode, struct inode_disk *inode_disk) {
  // Read on-disk inode from the buffer cache correspondin to the inode->sector
  // And return true
  return bc_read(inode->sector, inode_disk, 0, sizeof(struct inode_disk), 0);
}

static void locate_byte (off_t pos, struct sector_location *sec_loc) {
  off_t pos_sector = pos / SECTOR_SIZE;

  // Direct block
  if (pos_sector < DIRECT_BLOCK_ENTRIES) {
    // Update sec_loc
    sec_loc->directness = NORMAL_DIRECT;
    sec_loc->index1 = pos_sector;
    sec_loc->index2 = -1;
  }
  // Indirect block
  else if (pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES) {
    // Update sec_loc
    sec_loc->directness = INDIRECT;
    sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
    sec_loc->index2 = -1;
  }
  // Double Indirect block
  else if (pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES * (1 + INDIRECT_BLOCK_ENTRIES)) {
    // Update sec_loc
    sec_loc->directness = DOUBLE_INDIRECT;
    sec_loc->index1 = (pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES) / INDIRECT_BLOCK_ENTRIES;
    sec_loc->index2 = (pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES) % INDIRECT_BLOCK_ENTRIES;
  }
  // OUT_LIMIT
  else {
    // Update sec_loc
    sec_loc->directness = OUT_LIMIT;
    sec_loc->index1 = -1;
    sec_loc->index2 = -1;
  }
}

static inline off_t map_table_offset (int index) {
  return index*4;
}

static bool register_sector (struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc) {
  switch (sec_loc.directness)
  {
  case NORMAL_DIRECT:
    // Update new sector number to the inode_disk
    inode_disk->direct_map_table[sec_loc.index1] = new_sector;
    break;

  case INDIRECT:
    // For the first indirect block, allocate disk
    if (sec_loc.index1 == 0) {
      block_sector_t sector_idx;
      if(free_map_allocate(1, &sector_idx)) {
        inode_disk->indirect_block_sec = sector_idx;
      }
    }

    // Allocate new_block and write sector to the new_block
    block_sector_t *new_block = malloc(BLOCK_SECTOR_SIZE);
    if(new_block == NULL) {
      return false;
    }
    new_block[sec_loc.index1] = new_sector;
    
    // Write new_block to buffer cache
    bc_write(inode_disk->indirect_block_sec, (void *)new_block, map_table_offset(sec_loc.index1), 4, map_table_offset(sec_loc.index1));
    free(new_block);
    break;

  case DOUBLE_INDIRECT:
    // For the first double-indirect block, allocate disk
    if (sec_loc.index1 == 0 && sec_loc.index2 == 0) {
      block_sector_t sector_idx;
      if(free_map_allocate(1, &sector_idx)) {
        inode_disk->double_indirect_block_sec = sector_idx;
      }
    }
    // Update block 1.
    if (sec_loc.index2 == 0) {
      block_sector_t sector_idx;
      if(free_map_allocate(1, &sector_idx) == false) {
        return false;
      }
      block_sector_t *new_block = malloc(BLOCK_SECTOR_SIZE);
      if(new_block == NULL) {
        return false;
      }
      new_block[sec_loc.index1] = sector_idx;
      bc_write(inode_disk->double_indirect_block_sec, (void *)new_block, map_table_offset(sec_loc.index1), 4, map_table_offset(sec_loc.index1));
      free(new_block);
    }

    // Update block 2
    block_sector_t *index_block1 = malloc(BLOCK_SECTOR_SIZE);
    if (index_block1 == NULL) {
      return false;
    }
    block_sector_t *new_block2 = malloc(BLOCK_SECTOR_SIZE);
    if (new_block2 == NULL) {
      return false;
    }
    // Read index_block1
    bc_read(inode_disk->double_indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);
    block_sector_t block_sector = index_block1[sec_loc.index1];
    // Write block2
    new_block2[sec_loc.index2] = new_sector;
    bc_write(block_sector, (void *)new_block2, map_table_offset(sec_loc.index2), 4, map_table_offset(sec_loc.index2));
    free(new_block2);
    free(index_block1);
    break;
  
  default:
    return false;
    break;
  }

  // Return true
  return true;
}

bool inode_update_file_length (struct inode_disk *inode_disk, off_t start_pos, off_t end_pos) {
  off_t size = end_pos - start_pos;
  off_t offset = start_pos;
  void *zeros = malloc(SECTOR_SIZE);
  if(zeros == NULL) {
    return false;
  }
  int chunk_size;
  memset(zeros, 0, SECTOR_SIZE);

  // Allocate the new disk block
  while (size > 0) {
    // offset in the disk block
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    // Get chunk size
    if(size >= SECTOR_SIZE) {
      chunk_size = SECTOR_SIZE - sector_ofs;
    }
    else {
      if (sector_ofs + size > SECTOR_SIZE) {
        chunk_size = SECTOR_SIZE - sector_ofs;
      }
      else {
        chunk_size = size;
      }
    }

    // sector_ofs > 0 already allocated disk block.
    if (sector_ofs == 0) {
      struct sector_location sec_loc;
      block_sector_t sector_idx;

      // Allocate new disk block
      if(free_map_allocate(1, &sector_idx) == true) {
        // Update disk block number.
        locate_byte(offset, &sec_loc);
        register_sector(inode_disk, sector_idx, sec_loc);
      }
      else {
        printf("Enter here?\n");
        free(zeros);
        return false;
      }

      // init new disk block to 0
      bc_write(sector_idx, zeros, 0, SECTOR_SIZE, 0);
    }

    // Update size & offset
    size -= chunk_size;
    offset += chunk_size;
  }

  free(zeros);
  return true;
}

static void free_inode_sectors (struct inode_disk *inode_disk) {
  // Double-Indirect block release
  if (inode_disk->double_indirect_block_sec > 0) {
    int i = 0;
    struct inode_indirect_block *index_block1;
    index_block1 = (struct inode_indirect_block *) malloc(BLOCK_SECTOR_SIZE);
    if (index_block1 == NULL) {
      return;
    }
    bc_read(inode_disk->double_indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);
    
    // Acces index block2 through index block1
    while(index_block1->map_table[i] > 0 && i < SECTOR_SIZE) {
      int j = 0;
      struct inode_indirect_block *index_block2;
      index_block2 = (struct inode_indirect_block *) malloc(BLOCK_SECTOR_SIZE);
      if (index_block2 == NULL) {
        return;
      }
      
      // Read index block2
      bc_read(index_block1->map_table[i], (void *)index_block2, 0, SECTOR_SIZE, 0);

      // Free disk block2
      while(index_block2->map_table[j] > 0 && j < SECTOR_SIZE) {
        free_map_release(index_block2->map_table[j], 1);
        j++;
      }

      // Free index block2
      free_map_release(index_block1->map_table[i], 1);
      i++;

      //free(index_block2);
    }
    // Free double-indirect block.
    free_map_release(inode_disk->double_indirect_block_sec, 1);

    //free(index_block1);
  }
  // Indirect block release
  if(inode_disk->indirect_block_sec > 0) {
    int i = 0;
    struct inode_indirect_block *index_block1;
    index_block1 = (struct inode_indirect_block *) malloc(BLOCK_SECTOR_SIZE);
    if (index_block1 == NULL) {
      return;
    }

    // Read index block 1
    bc_read(inode_disk->indirect_block_sec, (void *)index_block1, 0, SECTOR_SIZE, 0);

    // Free indirect mapped sector
    while(index_block1->map_table[i] > 0 && i < SECTOR_SIZE) {
      free_map_release(index_block1->map_table[i], i);
      i++;
    }

    // Free index block 1
    free_map_release(inode_disk->indirect_block_sec, 1);

    //free(index_block1);
  }
  // Direct block release
  int i = 0;
  while(inode_disk->direct_map_table[i] > 0 && i < SECTOR_SIZE) {
    free_map_release(inode_disk->direct_map_table[i], 1);
    i++;
  }
}