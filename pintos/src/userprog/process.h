#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"

#define STACK_MAX (1 << 23)

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void argument_stack(char **parse, int count, void **esp);
struct thread *get_child_process(int pid);
void remove_child_process (struct thread *cp);
bool handle_mm_fault (struct vm_entry *vme);
bool expand_stack(void* addr);
bool verify_stack(int32_t sp, int32_t addr);
#endif /* userprog/process.h */
