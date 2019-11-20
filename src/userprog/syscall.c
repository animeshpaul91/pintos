#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Added Header Files
#include "threads/init.h"
#include "userprog/process.h" /* To invoke process_execute */
#include "threads/malloc.h"   /* For malloc allocation */
#include "devices/shutdown.h" /* For shutdown */
#include "devices/input.h"    /* For Input */
#include "filesys/file.h"     /* To allow file write */
#include "filesys/filesys.h"  /* For file operations */
#include "threads/vaddr.h"    /* For is_user_vaddr() */
#include "userprog/pagedir.h"

//Added File Descriptor Mapper begins
typedef struct file_des_mapper
{
    int fd;
    struct file *f;
    struct list_elem elem;
} map;
//Added Ends

//Added Prototypes Begin (13 System Calls)
static void halt(void);
/*static pid_t exec(const char *);
static int wait(pid_t);
static bool create(const char *, unsigned);
static bool remove(const char *);
static int open(const char *);
static int filesize(int);
static int read(int, void *, unsigned);*/
static int write(int, void *, unsigned);
/*static void seek(int, unsigned);
static unsigned tell(int);
static void close(int);*/
//Added Prototypes End
//Other helper functions
static void safe_mem_access(int *);
static bool validate_address(void *);

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *sp = (int *)f->esp; /* Get Current Stack Pointer */
  safe_mem_access(sp);

  switch(*sp)
  {
    case SYS_HALT:
    {
      halt();
      break;
    }
    case SYS_WRITE:
    {
      f->eax = write(*(sp + 1), (char *)*(sp + 2), *(sp + 3));
      break;
    }
    case SYS_EXIT:
    {
      exit(*(sp + 1));
      break;
    }
    default:
      printf("error %d", (*(int*)f->esp)); 
  }
}

static bool validate_address(void *address)
{
  struct thread *curr = thread_current();
  /* checks if address is within PHYS_BASE and in the thread's page */
  return (is_user_vaddr(address) && pagedir_get_page(curr->pagedir, address) != NULL);
}

static void safe_mem_access(int *sp)
{
  int safe_metric = validate_address(sp) + validate_address(sp + 1) + validate_address(sp + 2) + validate_address(sp + 3);
  if (safe_metric != 4)
    exit(-1);
}

void exit(int status)
{
  thread_current()->error_status = status;
  printf("%s: exit(%d)\n", thread_current()->name, status);
  sema_up(&thread_current()->parent->parent_sema);
  thread_exit();  
}

static void halt(void)
{
  shutdown_power_off();
}

/*static pid_t exec(const char *file)
{
  pid_t pid = -1;
  if (!validate_address((void *)file))
    exit(-1);
  struct thread *curr = thread_current();
  curr->exec_called = true;
  pid = process_execute(file); //this will call a sema_up() on load() increasing the initial value of 0 to 1.
  sema_down(&curr->parent_sema);
  curr->exec_called = false;
  return ((curr->exec_success)? pid: -1);
}*/

/*static int wait(pid_t pid)
{
  return (process_wait(pid));
}

static bool create(const char *file, unsigned initial_size)
{
  return true;
}

static bool remove(const char *file)
{
  return true;
}

static int open(const char *file)
{
  return -1;
}

static int filesize(int fd)
{
  return -1;
}

static int read(int fd, void *buffer, unsigned size)
{
  return -1;
} */

static int write(int fd, void *buffer, unsigned size)
{
  int ret = -1;
  if (buffer == NULL || !validate_address((void *) buffer))
    exit(-1);
  else if (fd == 1) //write to System console
  {
    putbuf(buffer, size);
    ret = 1;
  }
  /*else
  {
    map *df = get_file(fd, false);
    if (df != NULL)
      ret = file_write(df->f, buffer, size);
  }*/
  return (ret);
}

/* static void seek(int fd, unsigned position)
{

}

static unsigned tell(int fd)
{
  return 1;
}

static void close(int fd)
{

}

void *get_file(int fd, bool flag)
{
  struct thread *curr = thread_current();
  struct list map_list = curr->desc_file_map;
  map *fdmap = NULL;
  struct list_elem *l;

  if (list_empty(&map_list))
    return NULL;
  for (l = list_begin(&map_list); l != list_end(&map_list); l = list_next(l))
  {
    fdmap = list_entry(l, map, elem);
    if (fd == fdmap->fd)
      break;
  }
  if (fd == fdmap->fd)
    return ((flag) ? (void *)fdmap->f: (void *)fdmap);
  return NULL;
}*/