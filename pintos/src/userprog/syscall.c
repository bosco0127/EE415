#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/off_t.h"


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
  //printf("herereererwerw\n");
  int int_num = *(int32_t *)f->esp;
  //printf("here1\n");
  void *esp = f->esp;
  //printf("%d\n", int_num);
  //printf("here2\n");
  int arg[5];
  // arg 초기화
  int cnt = 4;
  while(cnt>-1)
  {
    arg[cnt] = 0;
    cnt--;
  }
    //hex_dump(f->esp, f->esp, 100, 1); 
  switch (int_num)
  {
  case SYS_HALT:
      // arg = 0
      halt();
    break;
  case SYS_EXIT:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      exit((int)*(uint32_t *)arg[0]);
    break;
  case SYS_EXEC:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = exec((const char *)*(uint32_t *)arg[0]);
    break;
  case SYS_WAIT:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = wait((pid_t)*(uint32_t*)arg[0]);
    break;
  case SYS_CREATE:
      // arg = 2;
      check_address(esp+4);
      get_argument(esp, arg, 2);
      f->eax = create((const char *)*(uint32_t *)arg[0], (unsigned)*(uint32_t *)arg[1]);
    break;
  case SYS_REMOVE:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = remove((const char *)*(uint32_t *)arg[0]);
    break;
  case SYS_OPEN:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = open((const char*)*(uint32_t *)arg[0]);
    break;
  case SYS_FILESIZE:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = filesize((int)*(uint32_t*)arg[0]);
    break;
  case SYS_READ:
      // arg = 3
      check_address(esp+4);
      get_argument(esp, arg, 3);
      f->eax = read((int)*(uint32_t *)arg[0], (void *)*(uint32_t*)arg[1], (unsigned)*(uint32_t*)arg[2]);
    break;
  case SYS_WRITE:
      // arg = 3
      check_address(esp+4);
      get_argument(esp, arg, 3);
      f->eax = write((int)*(uint32_t *)arg[0], (const void *)*(uint32_t*)arg[1], *(uint32_t*)arg[2]);
    break;
  case SYS_SEEK:
      // arg = 2
      check_address(esp+4);
      get_argument(esp, arg, 2);
      seek((int)*(uint32_t*)arg[0], (unsigned)*(uint32_t*)arg[1]);
    break;
  case SYS_TELL:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      f->eax = tell((int)*(uint32_t*)arg[0]);
    break;
  case SYS_CLOSE:
      // arg = 1
      check_address(esp+4);
      get_argument(esp, arg, 1);
      close((int)*(uint32_t*)arg[0]);
    break;
    
  case SYS_SIGACTION:
      check_address(esp+4);
      get_argument(esp, arg, 2);
      sigaction((int)*(uint32_t *)arg[0], (void *)*(uint32_t *)arg[1]);
    break;
  case SYS_SENDSIG:
      check_address(esp+4);
      get_argument(esp, arg, 2);
      sendsig((pid_t)*(uint32_t *)arg[0], (int)*(uint32_t *)arg[1]);
    break;
  case SYS_YIELD:
      sched_yield();
    break;
  }
}

// 유저 영역을 벗어났는지 확인
void check_address(void *addr)
{
  if(!is_user_vaddr(addr))
  {
    //exit 아직 구현 안되서 에러남
    exit(-1);
    //printf("error\n");
  }
}
// 수정 가능성 있음
void get_argument(void *esp, int *arg, int count)
{
  // esp는 첫 instruction number을 가리키므로 +4 먼저 함
  // +4 하는 과정에서 check_address 먼저
  check_address(esp+4);
  esp = esp+4;
  while(count)
  {
    *arg = (int *)esp;
    arg++;
    check_address(esp+4);
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
  check_address(file);
  if(file==NULL)
  {
    exit(-1);
  }
  bool success = filesys_create(file, initial_size);
  return success;
}

bool
remove (const char *file)
{
  check_address(file);
  if(file==NULL)
  {
    exit(-1);
  }
  bool success = filesys_remove(file);
  return success;
}

int open (const char *file) {
  int i;
  int ret = -1;
  struct file* fp;
  if (file == NULL) {
      exit(-1);
  }
  check_address(file);
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
  lock_acquire(&filesys_lock);
  check_address(buffer);
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
  check_address(buffer);
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
