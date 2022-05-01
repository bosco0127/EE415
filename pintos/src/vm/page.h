#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "lib/kernel/hash.h"

#define VM_BIN  0
#define VM_FILE 1
#define VM_ANON 2

struct vm_entry{
    uint8_t type; // VM_BIN, VM_FILE, VM_ANON
    void *vaddr; // vm_entry VPN
    bool writable; // true: write enable, false: write disable

    bool is_loaded; // flag whether it's loaded on the physical memory
    struct file *file; // file pointer

    struct list_elem mmap_elem; // memory map list element

    size_t offset; // file offset
    size_t read_bytes; // size of data in a virtual page
    size_t zero_bytes; // rest of the byte

    size_t swap_slot; // swap slot

    struct hash_elem elem; // hashtable element
};

/* Additional Functions */
void vm_init(struct hash *vm);
void vm_destroy(struct hash *vm);
struct vm_entry* find_vme(void *vaddr);
bool insert_vme(struct hash *vm, struct vm_entry *vme);
bool delete_vme(struct hash *vm, struct vm_entry *vme);
static unsigned vm_hash_func(const struct hash_elem *e, void *aux UNUSED);
static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func(struct hash_elem *e, void *aux UNUSED);

#endif /* vm/page.h */
