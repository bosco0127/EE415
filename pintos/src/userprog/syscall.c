#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/off_t.h"
#include "vm/page.h"

struct file
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
  };


// add
/* Invokes syscall NUMBER, passing no arguments, and returns the
   return value as an `int'. */
#define syscall0(NUMBER)                                        \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[number]; int $0x30; addl $4, %%esp"       \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER)                          \
               : "memory");                                     \
          retval;                                               \
        })

/* Invokes syscall NUMBER, passing argument ARG0, and returns the
   return value as an `int'. */
#define syscall1(NUMBER, ARG0)                                           \
        ({                                                               \
          int retval;                                                    \
          asm volatile                                                   \
            ("pushl %[arg0]; pushl %[number]; int $0x30; addl $8, %%esp" \
               : "=a" (retval)                                           \
               : [number] "i" (NUMBER),                                  \
                 [arg0] "g" (ARG0)                                       \
               : "memory");                                              \
          retval;                                                        \
        })

/* Invokes syscall NUMBER, passing arguments ARG0 and ARG1, and
   returns the return value as an `int'. */
#define syscall2(NUMBER, ARG0, ARG1)                            \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[arg1]; pushl %[arg0]; "                   \
             "pushl %[number]; int $0x30; addl $12, %%esp"      \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER),                         \
                 [arg0] "r" (ARG0),                             \
                 [arg1] "r" (ARG1)                              \
               : "memory");                                     \
          retval;                                               \
        })

/* Invokes syscall NUMBER, passing arguments ARG0, ARG1, and
   ARG2, and returns the return value as an `int'. */
#define syscall3(NUMBER, ARG0, ARG1, ARG2)                      \
        ({                                                      \
          int retval;                                           \
          asm volatile                                          \
            ("pushl %[arg2]; pushl %[arg1]; pushl %[arg0]; "    \
             "pushl %[number]; int $0x30; addl $16, %%esp"      \
               : "=a" (retval)                                  \
               : [number] "i" (NUMBER),                         \
                 [arg0] "r" (ARG0),                             \
                 [arg1] "r" (ARG1),                             \
                 [arg2] "r" (ARG2)                              \
               : "memory");                                     \
          retval;                                               \
        })



static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int int_num = *(int32_t *)f->esp;
  void *esp = f->esp;
  int arg[5];
  // arg 초기화
  int cnt = 4;
  while(cnt>-1)
  {
    arg[cnt] = 0;
    cnt--;
  }
  check_address(esp, esp);
  switch (int_num)
  {
  case SYS_HALT:
      // arg = 0
      halt();
    break;
  case SYS_EXIT:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      exit((int)*(uint32_t *)arg[0]);
    break;
  case SYS_EXEC:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      check_valid_string((const void *)*(uint32_t *)arg[0], esp);
      f->eax = exec((const char *)*(uint32_t *)arg[0]);
      // unpin_string((const void *)*(uint32_t *)arg[0]);
    break;
  case SYS_WAIT:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      f->eax = wait((pid_t)*(uint32_t*)arg[0]);
    break;
  case SYS_CREATE:
      // arg = 2;
      get_argument(esp, arg, 2, esp);
      check_valid_string((const void *)*(uint32_t *)arg[0], esp);
      f->eax = create((const char *)*(uint32_t *)arg[0], (unsigned)*(uint32_t *)arg[1]);
      // unpin_string((const void *)*(uint32_t *)arg[0]);
    break;
  case SYS_REMOVE:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      check_valid_string((const void *)*(uint32_t *)arg[0], esp);
      f->eax = remove((const char *)*(uint32_t *)arg[0]);
    break;
  case SYS_OPEN:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      check_valid_string((const void *)*(uint32_t *)arg[0], esp);
      f->eax = open((const char*)*(uint32_t *)arg[0]);
      // unpin_string((const void *)*(uint32_t *)arg[0]);
    break;
  case SYS_FILESIZE:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      f->eax = filesize((int)*(uint32_t*)arg[0]);
    break;
  case SYS_READ:
      // arg = 3
      get_argument(esp, arg, 3, esp);
      check_valid_buffer((void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2], true, esp);
      f->eax = read((int)*(uint32_t *)arg[0], (void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2]);
      unpin_buffer((void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2]);
    break;
  case SYS_WRITE:
      // arg = 3
      get_argument(esp, arg, 3, esp);
      check_valid_buffer((void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2], false, esp);
      f->eax = write((int)*(uint32_t *)arg[0], (const void *)*(uint32_t*)arg[1], *(uint32_t*)arg[2]);
      unpin_buffer((void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2]);
    break;
  case SYS_SEEK:
      // arg = 2
      get_argument(esp, arg, 2, esp);
      seek((int)*(uint32_t*)arg[0], (unsigned)*(uint32_t*)arg[1]);
    break;
  case SYS_TELL:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      f->eax = tell((int)*(uint32_t*)arg[0]);
    break;
  case SYS_CLOSE:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      close((int)*(uint32_t*)arg[0]);
    break;
    
  case SYS_SIGACTION:
      get_argument(esp, arg, 2, esp);
      sigaction((int)*(uint32_t *)arg[0], (void *)*(uint32_t *)arg[1]);
    break;
  case SYS_SENDSIG:
      get_argument(esp, arg, 2, esp);
      sendsig((pid_t)*(uint32_t *)arg[0], (int)*(uint32_t *)arg[1]);
    break;
  case SYS_YIELD:
      sched_yield();
    break;
  case SYS_MMAP:
      // arg = 2
      get_argument(esp, arg, 2, esp);
      f->eax = mmap((int)*(uint32_t*)arg[0], (void *)*(uint32_t*)arg[1]);
    break;
  case SYS_MUNMAP:
      // arg = 1
      get_argument(esp, arg, 1, esp);
      munmap((mapid_t)*(uint32_t*)arg[0]);
    break;    
  }
  unpin_addr(f->esp);
}

// 유저 영역을 벗어났는지 확인
struct vm_entry *check_address(void *addr, void *esp)
{
  /* Exit if addr is not in user space */
  if(addr < (void *)0x08048000 || addr >= (void *)0xc0000000) {
    exit(-1);
  }
  struct vm_entry *vme;

  /* find vme from hashtable */
  vme = find_vme(addr);

  /* check vme is exist */
  if(vme == NULL) {
    // Verify it expands stack
     if(!verify_stack((int32_t)esp, (int32_t)addr)) {
       exit(-1);
     }
     // Expand stack
     vme = expand_stack(addr);
     if (vme == NULL) {
        exit(-1);
     }
  }

  return vme;
}

// Check buffer address is valid
void check_valid_buffer(void *buffer, unsigned size, bool to_write, void *esp){
  struct vm_entry *vme;
  char *check_addr = (char *)buffer;
  int i;

  /* check all addresses from buffer to buffer+size-1 */
  for(i=0; i<(int)size; i++){
    /* Get vm_entry from check_address */
    vme = check_address((void *)check_addr, esp);
    
    /* Check writable if to_write is true */
    if(to_write){
      if(!vme->writable){
        exit(-1);
      }
    }

    /* Keep Checking for the rest of them */
    check_addr++;
  }
}

// Check if string address is valid
void check_valid_string(const void *str, void *esp){
  char *check_addr = (char *)str;
  check_address((void *)check_addr, esp);
  while(*check_addr != 0){
    check_addr++;
    check_address((void *)check_addr, esp);
  }
}

// Pin vm_entry for vaddr
void pin_addr(void *vaddr) {
  struct vm_entry *vme = find_vme(vaddr);
  vme->pinned = true;
  if(vme->is_loaded == false) {
    handle_mm_fault(vme);
  }
}

// Pin whole buffer
void pin_buffer(void *buffer, unsigned size) {
  char *vaddr = (char *)buffer;
  int i;
  for(i=0; i < size; i++) {
    pin_addr((void *)vaddr);
    vaddr++;
  }
}

// Unpin vm_entry for vaddr
void unpin_addr(void *vaddr) {
  struct vm_entry *vme;
  vme = find_vme(vaddr);
  if(vme != NULL) {
    vme->pinned = false;
  }
}

// Unpin whole buffer
void unpin_buffer(void *buffer, unsigned size) {
  char *vaddr = (char *)buffer;
  int i;
  for(i=0; i < size; i++) {
    unpin_addr((void *)vaddr);
    vaddr++;
  }
}

// Unpin string
/*void unpin_string(const void *str) {
  char *vaddr = (char *)str;
  unpin_addr((void *)vaddr);
  while(*vaddr != 0){
    vaddr++;
    unpin_addr((void *)vaddr);
  }
}*/

// 수정 가능성 있음
void get_argument(void *esp, int *arg, int count, void *f_esp)
{
  // esp는 첫 instruction number을 가리키므로 +4 먼저 함
  // +4 하는 과정에서 check_address 먼저
  check_address(esp+4, f_esp);
  esp = esp+4;
  while(count)
  {
    *arg = (int *)esp;
    arg = arg + 1;
    check_address(esp+4, f_esp);
    esp=esp+4;
    count--;
  }
}

// add

void
halt (void) 
{
  shutdown_power_off();
}

void
exit (int status)
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  if(cur->exit_status != -1) {
    cur->exit_status = status;
  }
  printf("%s: exit(%d)\n", thread_name(), cur->exit_status);
  thread_exit();
}

pid_t
exec (const char *file)
{
  return process_execute(file);
}

int
wait (pid_t pid)
{
  int wait_result = process_wait(pid);
  //printf("status:%d\n", wait_result);
  return wait_result;
}

bool
create (const char *file, unsigned initial_size)
{
  if(file==NULL)
  {
    exit(-1);
  }
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);

  return success;
}

bool
remove (const char *file)
{
  if(file==NULL)
  {
    exit(-1);
  }
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);

  return success;
}

int open (const char *file) {
  int i;
  int ret = -1;
  struct file* fp;
  if (file == NULL) {
      exit(-1);
  }
  lock_acquire(&filesys_lock);
  fp = filesys_open(file);
  if (fp == NULL) {
      ret = -1;
  } else {
    for (i = 3; i < 64; i++) {
    //for (i = 3; i < 10; i++) {
      if (thread_current()->fd[i] == NULL) {
        thread_current()->fd[i] = fp;
        ret = i;
        break;
      }
    }
  }
  lock_release(&filesys_lock);
  return ret;
}

int
filesize (int fd) 
{
  if(thread_current()->fd[fd]==NULL)
  {
    exit(-1);
  }
  return file_length(thread_current()->fd[fd]);
}

int read (int fd, void* buffer, unsigned size) {
  int i;
  int ret;
  pin_buffer(buffer, size);
  lock_acquire(&filesys_lock);
  if (fd == 0) {
    for (i = 0; i < size; i ++) {
      if (input_getc() == '\0') {
        break;
      }
    }
    ret = i;
  } else if (fd > 2) {
    if (thread_current()->fd[fd] == NULL) {
      lock_release(&filesys_lock);
      exit(-1);
    }
    ret = file_read(thread_current()->fd[fd], buffer, size);
  }
  lock_release(&filesys_lock);
  return ret;
}

int write (int fd, const void *buffer, unsigned size) {

  int ret = -1;
  pin_buffer(buffer, size);
  lock_acquire(&filesys_lock);
  if (fd == 1) {
    putbuf(buffer, size);
    ret = size;
  } else if (fd > 2) {
    if (thread_current()->fd[fd] == NULL) {
      lock_release(&filesys_lock);
      exit(-1);
    }
    if (thread_current()->fd[fd]->deny_write) {
        file_deny_write(thread_current()->fd[fd]);
    }
    ret = file_write(thread_current()->fd[fd], buffer, size);
  }
  lock_release(&filesys_lock);
  return ret;
}

void
seek (int fd, unsigned position) 
{
  if(thread_current()->fd[fd]==NULL)
  {
    exit(-1);
  }
  file_seek(thread_current()->fd[fd], position);
}

unsigned
tell (int fd) 
{
  if(thread_current()->fd[fd]==NULL)
  {
    exit(-1);
  }
  return file_tell(thread_current()->fd[fd]);
}

void
close (int fd)
{
  struct file *fp = thread_current()->fd[fd];
  if(fp==NULL)
  {
    exit(-1);
  }
  thread_current()->fd[fd] = NULL;
  return file_close(fp);
}

void sigaction (int signum, void (*handler) (void))
{
  struct thread *cur = thread_current();
  cur->handler_address[signum-1] = handler;
}

void sendsig (pid_t pid, int signum)
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  struct thread *t;
  for (e = list_begin(&cur->child); e != list_end(&cur->child); e = list_next(e)) {
    t = list_entry(e, struct thread, child_elem);
    if(t->tid == pid) {
      break;
    }
  }
  if ( t -> handler_address[signum-1] != 0) {
    printf("Signum: %d, Action: %p\n",signum, (int *)t -> handler_address[signum-1]);
  }
}

void sched_yield ()
{
  thread_yield();
}

int
mmap (int fd, void *addr)
{
  // Check fd is 2~63
  if(fd < 2 || fd > 63) {
    return -1;
  }

  // Check addr: page aligned, already in use, is 0.
  if((uint32_t)addr%PGSIZE != 0 || find_vme(addr) != NULL || addr == NULL) {
    return -1;
  }

  struct thread *cur = thread_current();
  struct mmap_file *new_mmap_file;
  struct vm_entry *new_vme;
  struct file *file_reopened;
  void *vaddr = addr;
  size_t file_size;
  size_t page_read_bytes;
  size_t page_zero_bytes;
  off_t offset = 0;
  struct list_elem *e;
  struct vm_entry *vme;

  // Reopen file & Check validation
  file_reopened = file_reopen(cur->fd[fd]);
  if(file_reopened == NULL) {
    return -1;
  }

  // Get file size & if zero, return -1
  file_size = file_length(file_reopened);
  if(file_size <= 0) {
    return -1;
  }

  // Allocate new_mmap_file
  new_mmap_file = (struct mmap_file *) malloc(sizeof(struct mmap_file));
  if(new_mmap_file == NULL) {
    return -1;
  }
  // Allocate mapid
  new_mmap_file->mapid = cur->mapid;
  cur->mapid++;
  // Initialize vme_list of new_mmap_file
  list_init(&new_mmap_file->vme_list);
  // Initialize file pointer of new_mmap_file
  new_mmap_file->file = file_reopened;

  // Allocate vm_entries
  while(file_size > 0) {
    // Allocate memory to the vme
    new_vme = (struct vm_entry *) malloc(sizeof(struct vm_entry));
    if(new_vme == NULL) {
      for(e = list_begin(&new_mmap_file->vme_list); e = !list_end(&new_mmap_file->vme_list); e = list_next(e)) {
        vme = list_entry(e, struct vm_entry, mmap_elem);
        free(vme);
      }
      free(new_mmap_file);
      return -1;
    }

    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    page_read_bytes = file_size < PGSIZE ? file_size : PGSIZE;
    page_zero_bytes = PGSIZE - page_read_bytes;
    /* Setting vm_entry members, offset and size of file to read when virtual
       page is requitred, zero byte to pad at the end, ... */
    new_vme->type = VM_FILE;
    new_vme->vaddr = vaddr;
    new_vme->writable = true;
    new_vme->is_loaded = false;
    new_vme->pinned = false;
    new_vme->file = file_reopened; //file;
    new_vme->offset = offset;
    new_vme->read_bytes = page_read_bytes;
    new_vme->zero_bytes = page_zero_bytes;

    /* Add vm_entry to hash table by insert_vme() */
    if(!insert_vme(&cur->vm,new_vme)) {
	    free(new_vme);
      for(e = list_begin(&new_mmap_file->vme_list); e = !list_end(&new_mmap_file->vme_list); e = list_next(e)) {
        vme = list_entry(e, struct vm_entry, mmap_elem);
        free(vme);
      }
      free(new_mmap_file);
      return -1;
    }

    // Insert new_vme to new_mmap vme_list
    list_push_back(&new_mmap_file->vme_list, &new_vme->mmap_elem);

    /* Advance. */
    offset += page_read_bytes;
    file_size -= page_read_bytes;
    vaddr += PGSIZE;
  }

  // Insert new_vme to new_mmap vme_list
  list_push_back(&cur->mmap_list, &new_mmap_file->elem);

  return new_mmap_file->mapid;
}

void do_munmap(struct mmap_file *mmap_file) {
  struct thread *cur = thread_current();
  struct vm_entry *vme;
  struct list_elem *e;
  struct list_elem *temp;
  void *paddr;

  if(mmap_file == NULL) {
    return;
  }

  // Remove all vm_entry in the vme_list
  //for(e = list_begin(&mmap_file->vme_list); e != list_end(&mmap_file->vme_list); e = list_next(e)) {
  for(e = list_begin(&mmap_file->vme_list); e != list_end(&mmap_file->vme_list);) {
    vme = list_entry(e, struct vm_entry, mmap_elem);

    // Check if it is loaded.
    if(vme->is_loaded == true) {

      // Check if the page is dirty
      // If it is dirty, write back to the file.
      paddr = pagedir_get_page(cur->pagedir, vme->vaddr);
      if(pagedir_is_dirty(cur->pagedir, vme->vaddr) == true) {
        lock_acquire(&filesys_lock);
        //file_write_at(mmap_file->file, paddr/*vme->vaddr*/, vme->read_bytes, vme->offset);
        file_write_at(mmap_file->file, paddr/*vme->vaddr*/, 4096, vme->offset);
        lock_release(&filesys_lock);
      }

      // Clear the page
      pagedir_clear_page(cur->pagedir, vme->vaddr);

      // free page
      free_page(paddr);
    }

    // Remove from the vme_list
    temp = list_next(e); // trouble in list_next()
    list_remove(e);
    e = temp;

    // Remove from the vm of the current thread.
    delete_vme(&cur->vm, vme);
  }
}

void
munmap (mapid_t mapid)
{
  struct thread *cur = thread_current();
  struct mmap_file *mmap;
  struct list_elem *e;
  struct list_elem *temp;

  if(list_empty(&cur->mmap_list)) {
    return;
  }

  // Remove all mmap_file in the cur->mmap_list
  //for(e = list_begin(&cur->mmap_list); e != list_end(&cur->mmap_list); e = list_next(e)) {
  for(e = list_begin(&cur->mmap_list); e != list_end(&cur->mmap_list);) {
    mmap = list_entry(e, struct mmap_file, elem);

    // Check if mapid is matched or -1(close all mmap file)
    if(mapid == -1 || mmap->mapid == mapid) {

      // Remove all vm_entry from the mmap
      do_munmap(mmap);
      
      // Close file
      lock_acquire(&filesys_lock);
      file_close(mmap->file);
      lock_release(&filesys_lock);

      // remove from the cur->mmap_list
      temp = list_next(e);
      list_remove(e);
      e = temp;

      // Deallocate the memory
      free(mmap);

      // Break if mapid is not for closin all the mmap file
      if(mapid != -1) {
        break;
      }
    }
  }
}