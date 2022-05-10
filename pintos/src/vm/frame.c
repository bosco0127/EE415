#include <stdio.h>
#include <threads/malloc.h>
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

// lru_list as a global variable.
struct list lru_list;
// lock for lru_list.
struct lock lru_list_lock;
// page for lru_list: Clock Algorithm
struct page *lru_clock;

void lru_list_init(void){
  // Initialize list
  list_init(&lru_list);
  // Initialize lock
  lock_init(&lru_list_lock);
  // Initialize page as NULL
  lru_clock = NULL;
}

void add_page_to_lru_list(struct page* page){
  // Insert page at the end of the lru_list
  if(page != NULL){
    lock_acquire(&lru_list_lock);
    list_push_back(&lru_list, &page->lru);
    lock_release(&lru_list_lock);
  }
}

void del_page_from_lru_list(struct page* page){
  struct list_elem *elem;
  // Remove user page from the lru_list
  if(page != NULL){
    elem = list_remove(&page->lru);
    // If the page is lru_clock, update lru_lock.
    if(lru_clock == page){
      lru_clock = list_entry(elem, struct page, lru);
    } 
  }
}

struct page *alloc_page(enum palloc_flags flags, bool is_huge){
  struct page *new;
  void *kaddr;

  // Check if flag is for user space
  if((flags & PAL_USER) == 0){
    return NULL;
  }

  // allocate page w/ palloc_get_page()
  if(is_huge == true) {
    do {
      kaddr = palloc_get_page(flags);
    } while ((uint32_t) kaddr % 0x400000 != 0);

    palloc_free_page(kaddr);
    kaddr = palloc_get_multiple(flags, 1024);

    ASSERT((uint32_t)kaddr % 0x400000 == 0);

    // Keep trying allocating pages
    while(kaddr == NULL){
      try_to_free_pages(flags);
      kaddr = palloc_get_multiple(flags, 1024); 
    }
    // allocate page structure, initialize
    new = malloc(sizeof(struct page));
    if(new == NULL){
      palloc_free_multiple(kaddr, 1024); 
      return NULL;
    }
  } else {
    kaddr = palloc_get_page(flags); //multiple(flags, 1024);
    // Keep trying allocating pages
    while(kaddr == NULL){
      try_to_free_pages(flags);
      kaddr = palloc_get_page(flags); //multiple(flags, 1024); 
    }
    // allocate page structure, initialize
    new = malloc(sizeof(struct page));
    if(new == NULL){
      palloc_free_page(kaddr); //multiple(kaddr, 1024); 
      return NULL;
    }
  }
  new->kaddr = kaddr;
  new->thread = thread_current();

  // Insert page into lru_list w/ add_page_to_lru_list
  add_page_to_lru_list(new);

  // return address of page structure 
  return new;
}

void free_page(void *kaddr, bool is_huge) {
  struct page *p;
  struct list_elem *e;

  lock_acquire(&lru_list_lock);
  // Search page in lru_list by kaddr
  for(e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e)){
    p = list_entry(e, struct page, lru);
    if(p->kaddr == kaddr){
      // Call __free_page()
      __free_page(p, is_huge);
      break;
    }
  }
  lock_release(&lru_list_lock);
}

void __free_page(struct page* page, bool is_huge) {
  // vme->unloaded
  page->vme->is_loaded = false;
  page->vme->pinned = false;

  // Remove page from lru_list
  del_page_from_lru_list(page);

  // Deallocate memory of page
  if(is_huge == true) {
    palloc_free_multiple(page->kaddr, 1024); 
    free(page);
  } else {
    palloc_free_page(page->kaddr); //multiple(page->kaddr, 1024); 
    free(page);
  }
}

// Returns next node of lru
// Returns NULL if lru is at the end of the list
static struct list_elem *get_next_lru_clock(){
  struct list_elem *next_lru;
  
  // If lru_clock is NULL
  if(lru_clock == NULL){
    next_lru = list_begin(&lru_list);
    if(next_lru != list_end(&lru_list)){
      lru_clock = list_entry(next_lru, struct page, lru);
      return next_lru;
    }
    else {
      return NULL;
    }
  }
  
  // If lru_clock is not NULL
  next_lru = list_next(&lru_clock->lru);
  if(next_lru == list_end(&lru_list)){
    if(&lru_clock->lru == list_begin(&lru_list)){
      return NULL;
    }
    else {
      next_lru = list_begin(&lru_list);
    }
  }
  lru_clock = list_entry(next_lru, struct page, lru);
  return next_lru;
}


// Get free page with Clock Algorithm
void try_to_free_pages(enum palloc_flags flags){
  struct thread *thread_lru;
  struct list_elem *element;
  struct page *lru_page;

  // Check if lru_list is empty
  if(list_empty(&lru_list) == true){
    return;
  }

  lock_acquire(&lru_list_lock);
  while(1){
    // get next lru element
    element = get_next_lru_clock();
    if(element == NULL){
      lock_release(&lru_list_lock);
      return;
    }
    
    // get LRU page w/ list_entry
    lru_page = list_entry(element, struct page, lru);
    // debug //
    /*if(lru_page->vme == NULL) {
      continue;
    }*/

    // if pinned, it is not a victim.
    if(lru_page->vme->pinned == true){
      continue;
    }

    // if huged, it is not a victim.
    if(lru_page->vme->is_huge == true){
      continue;
    }

    // get thread that has lru page
    thread_lru = lru_page->thread;

    // if page is accessed, set it to 0 and continue, for later.
    if(pagedir_is_accessed(thread_lru->pagedir, lru_page->vme->vaddr)){
      pagedir_set_accessed(thread_lru->pagedir, lru_page->vme->vaddr, false);
      continue;
    }

    // if not, it is a victim
    // if page is dirty, or anonymous region,
    if(pagedir_is_dirty(thread_lru->pagedir, lru_page->vme->vaddr) || lru_page->vme->type == VM_ANON){
      // if file, write it to the file
      if(lru_page->vme->type == VM_FILE){
        lock_acquire(&filesys_lock);
	      file_write_at(lru_page->vme->file, lru_page->kaddr, lru_page->vme->read_bytes, lru_page->vme->offset);
	      lock_release(&filesys_lock);
      }
      // else, swap out
      else{
	      lru_page->vme->type = VM_ANON;
	      lru_page->vme->swap_slot = swap_out(lru_page->kaddr);
      }
    }

    // page is unloaded
    lru_page->vme->is_loaded = false;
    pagedir_clear_page(thread_lru->pagedir, lru_page->vme->vaddr);
    __free_page(lru_page, lru_page->vme->is_huge);
    break;
  }
  lock_release(&lru_list_lock);
  return;
}
