#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "devices/block.h"

struct lock swap_lock;
struct bitmap *swap_map;
struct block *swap_block;

extern struct lock filesys_lock;

void swap_init(void){
  // Get swap_block. 
  swap_block = block_get_role(BLOCK_SWAP);
  if(swap_block == NULL){
    return;
  }

  // Create bitmap
  swap_map = bitmap_create(block_size(swap_block) / SECTOR_PER_PAGE);
  if(swap_map == NULL){
    return;
  }

  // Initialize bitmap 
  bitmap_set_all(swap_map, SWAP_FREE);
  
  // Initialize lock value
  lock_init(&swap_lock);
}

void swap_in(size_t used_index, void *kaddr){
  int i;

  lock_acquire(&filesys_lock);
  lock_acquire(&swap_lock);	
  // Check if swap space if free
  if(bitmap_test(swap_map, used_index) == SWAP_FREE){
    return;
  }

  // Read from swap disk to physical memory
  for(i=0; i < SECTOR_PER_PAGE; i++)
  {
    block_read(swap_block, used_index * SECTOR_PER_PAGE + i, (uint8_t *)kaddr + i * BLOCK_SECTOR_SIZE);
  }

  // Set bitmap
  bitmap_flip(swap_map, used_index);
  lock_release(&swap_lock);
  lock_release(&filesys_lock);
}

size_t swap_out(void *kaddr){
  int i;
  size_t free_slot;
  
  lock_acquire(&filesys_lock);
  lock_acquire(&swap_lock);
  // Find SWAP_FREE index.
  free_slot = bitmap_scan_and_flip(swap_map, 0, 1, SWAP_FREE);
  // If there is no SWAP_FREE index, return
  if(free_slot == BITMAP_ERROR)
    return BITMAP_ERROR;
  
  // Write back to swap disk
  for(i=0; i < SECTOR_PER_PAGE; i++)
  {
    block_write(swap_block, free_slot * SECTOR_PER_PAGE + i, (uint8_t *)kaddr + i * BLOCK_SECTOR_SIZE);
  }
  lock_release(&swap_lock);
  lock_release(&filesys_lock);

  return free_slot;
}
