#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"

// Points the memory region of the buffer caches
void *p_buffer_cache;
// Array of buffer head
struct buffer_head head_buffer[BUFFER_CACHE_ENTRY_NB];
// Variable for clock algorithm
int clock_hand;
// Lock for clock hand
struct lock clock_hand_lock;

bool bc_read (block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs) {
  struct buffer_head *read_buffer;
  // Find buffer head which sector is equal to the sector_idx
  read_buffer = bc_lookup(sector_idx);

  // If don't, kick out the victim and read data from the disk.
  if(read_buffer == NULL) {
    read_buffer = bc_select_victim();
    lock_acquire(&read_buffer->buffer_lock);
    read_buffer->is_used = true;
    read_buffer->sector = sector_idx;
    block_read(fs_device, read_buffer->sector, read_buffer->data);
    lock_release(&read_buffer->buffer_lock);
  }

  lock_acquire(&read_buffer->buffer_lock);
  // Read data from the buffer cache.
  memcpy(buffer + bytes_read, read_buffer->data + sector_ofs, chunk_size);

  // Update clock bit
  read_buffer->clock_bit = true;
  lock_release(&read_buffer->buffer_lock);

  // return read_buffer
  return true;
}

bool bc_write (block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs) {
  struct buffer_head *write_buffer;
  // Find buffer head which sector is equal to the sector_idx
  write_buffer = bc_lookup(sector_idx);

  // If don't, kick out the victim and read data from the disk.
  if(write_buffer == NULL) {
    write_buffer = bc_select_victim();
    lock_acquire(&write_buffer->buffer_lock);
    write_buffer->is_used = true;
    write_buffer->sector = sector_idx;
    block_read(fs_device, write_buffer->sector, write_buffer->data);
    lock_release(&write_buffer->buffer_lock);
  }

  lock_acquire(&write_buffer->buffer_lock);
  // Write data to the buffer cache.
  memcpy(write_buffer->data + sector_ofs, buffer + bytes_written, chunk_size);

  // Update clock bit
  write_buffer->dirty = true;
  write_buffer->clock_bit = true;
  lock_release(&write_buffer->buffer_lock);

  // return read_buffer
  return true;
}

struct buffer_head *bc_lookup (block_sector_t sector) {
  int i;
  // Search buffer head whose sector is matched to the sector factor
  for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
    lock_acquire(&head_buffer[i].buffer_lock);
    if(head_buffer[i].is_used == true && head_buffer[i].sector == sector) {
      lock_release(&head_buffer[i].buffer_lock);
      // Retrun addres of the matching entries
      return &head_buffer[i];
    }
    lock_release(&head_buffer[i].buffer_lock);
  }

  // Return False if there's no matcing entries
  return NULL;
}

struct buffer_head *bc_select_victim () {
  int i;
  // Find unused buffer cache entry as a victim
  for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
    lock_acquire(&head_buffer[i].buffer_lock);
    if(head_buffer[i].is_used == false) {
      lock_release(&head_buffer[i].buffer_lock);
      return &head_buffer[i];
    }
    lock_release(&head_buffer[i].buffer_lock);
  }

  // Else choose victim by clock algorithm
  while(1) {
    lock_acquire(&clock_hand_lock);
    // Increase clock_hand by 1
    if(clock_hand != BUFFER_CACHE_ENTRY_NB - 1) {
      clock_hand++;
    }
    else {
      clock_hand = 0;
    }

    // Look around buffer head, check clock bit
    lock_acquire(&head_buffer[clock_hand].buffer_lock);
    if(head_buffer[clock_hand].clock_bit == false) {
      // If cache is dirty, flush it to the disk
      if(head_buffer[clock_hand].dirty == true) {
        bc_flush_entry(&head_buffer[clock_hand]);
      }

      // Update the buffer_head information
      head_buffer[clock_hand].is_used = true;
      head_buffer[clock_hand].sector = -1;
      head_buffer[clock_hand].clock_bit = true;

      // return victim
      lock_release(&head_buffer[clock_hand].buffer_lock);
      lock_release(&clock_hand_lock);
      return &head_buffer[clock_hand];
    }
    else {
      head_buffer[clock_hand].clock_bit = false;
    }
    lock_release(&head_buffer[clock_hand].buffer_lock);
    lock_release(&clock_hand_lock);
  }
}

void bc_flush_entry (struct buffer_head *p_flush_entry) {
  // Call block_write to flush data of the p_flush_entry into the disk
  block_write(fs_device, p_flush_entry->sector, p_flush_entry->data);
  // Update dirty bit of the buffer head
  p_flush_entry->dirty = false;
}

void bc_flush_all_entries () {
  int i;

  // Flush all the dirty entry of the buffer head array
  for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
    lock_acquire(&head_buffer[i].buffer_lock);
    if(head_buffer[i].is_used == true && head_buffer[i].dirty == true) {
      bc_flush_entry(&head_buffer[i]);
    }
    lock_release(&head_buffer[i].buffer_lock);
  }
}

void bc_init () {
  int i;

  // Allocate buffer cache in Memory
  p_buffer_cache = malloc(SECTOR_SIZE * BUFFER_CACHE_ENTRY_NB);
  if (p_buffer_cache == NULL) {
    ASSERT(0);
  }

  // Initialize buffer_head
  for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++) {
    head_buffer[i].dirty = false; 
    head_buffer[i].is_used = false;
    head_buffer[i].sector = -1;
    head_buffer[i].clock_bit = false;
    lock_init(&head_buffer[i].buffer_lock);
    head_buffer[i].data = p_buffer_cache + i * SECTOR_SIZE;
  }

  // Initialize clock_hand
  clock_hand = 0;

  // Initialize clock_hand_lock
  lock_init(&clock_hand_lock);
}

void bc_term () {
  // Flush all buffer cache entries
  bc_flush_all_entries();

  // Deallocate buffer cache memories
  free(p_buffer_cache);
}
