#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/thread.h"
#include "lib/kernel/hash.h"
#include "vm/page.h"

void vm_init(struct hash *vm){
  hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

void vm_destroy(struct hash *vm){
  hash_destroy(vm, vm_destroy_func);
}

struct vm_entry* find_vme(void *vaddr){
  struct hash_elem *elem_find;
  struct vm_entry vme;
  /* Get VPN of vaddr */
  vme.vaddr = pg_round_down(vaddr);
  /* find hash_elem */
  elem_find = hash_find(&thread_current()->vm, &vme.elem);
  /* Return vm_entry */
  if(elem_find != NULL){
    return hash_entry(elem_find, struct vm_entry, elem);
  }
  return NULL;
}

bool insert_vme(struct hash *vm, struct vm_entry *vme){
  bool success = false;
  struct hash_elem *result;
   
  /* hash_insert returns NULL if success */
  result = hash_insert(vm, &vme->elem);
  if(result == NULL){
    success = true;
  }

  return success;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme){
  bool success = false;
  struct hash_elem *result;
   
  /* hash_insert returns NULL if success */
  result = hash_delete(vm, &vme->elem);
  if(result != NULL){
    success = true;
    /* free vme */
    free(vme);
  }

  return success;
}

static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED){
  struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
  return hash_int((int)vme->vaddr);
}

static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
  struct vm_entry *vma = hash_entry(a, struct vm_entry, elem);
  struct vm_entry *vmb = hash_entry(b, struct vm_entry, elem);
  
  if(vma->vaddr < vmb->vaddr){
    return true;
  }
  else
    return false;  
}

static void vm_destroy_func(struct hash_elem *e, void *aux UNUSED){
  struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
  void *paddr;

  /* If it is loaded, deallocate page & page mapping */
  if(vme->is_loaded){
    paddr = pagedir_get_page(thread_current()->pagedir, vme->vaddr);
    /* free page */
    palloc_free_page(paddr);
    /* clear page directory */
    pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
  }

  /* free vm_entry */
  free(vme);
}
