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
#include "threads/synch.h"    /* For file_lock */
#include "userprog/pagedir.h"

//Added Prototypes Begin (13 System Calls)
static void halt(void);
static pid_t exec(const char *);
static int wait(pid_t);
static bool create(const char *, unsigned);
static bool remove(const char *);
static int open(const char *);
static int filesize(int);
static int read(int, void *, unsigned);
static int write(int, void *, unsigned);
static void seek(int, unsigned);
static unsigned tell(int);
static void close(int);
//Added Prototypes End

//Other helper functions start
static struct lock file_lock;
static void safe_mem_access(int *);
static bool validate_address(void *);
//Other helper functions ends

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  lock_init(&file_lock);
  int *sp = (int *)f->esp; /* Get Current Stack Pointer */
  safe_mem_access(sp);

  switch(*sp)
  {
    case SYS_HALT:
    {
      halt();
      break;
    }
    
    case SYS_EXIT:
    {
      exit(*(sp + 1));
      break;
    }
    
    case SYS_EXEC:
    {
      f->eax = exec((const char *) *(sp + 1));
      break;
    }

    case SYS_WAIT:
    {
      f->eax = wait(*(sp + 1));
      break;
    }

    case SYS_WRITE:
    {
      f->eax = write(*(sp + 1), (void *)*(sp + 2), *(sp + 3));
      break;
    }

    case SYS_CREATE:
    {
      f->eax = create((char *)*(sp + 1), *(sp + 2));
      break;
    }

    case SYS_REMOVE:
    {
      f->eax = remove((char *) *(sp + 1));
      break;
    }

    case SYS_OPEN:
    {
      f->eax = open((char *) *(sp + 1));
      break;
    }

    case SYS_FILESIZE:
    {
      f->eax = filesize(*(sp + 1));
      break;
    }

    case SYS_READ:
    {
      f->eax = read(*(sp + 1), (void *) *(sp + 2), *(sp + 3));
      break;
    }

    case SYS_SEEK:
    {
      seek(*(sp + 1), *(sp + 2));      
      break;
    }

    case SYS_TELL:
    {
      f->eax = tell(*(sp + 1));
      break;
    }

    case SYS_CLOSE:
    {
      close(*(sp + 1));
      break;
    }

    default:
            printf("default %d\n", *sp);
  }
}

static bool validate_address(void *address)
{
  struct thread *curr = thread_current();
  return (is_user_vaddr(address) && pagedir_get_page(curr->pagedir, address) != NULL);
}

static void safe_mem_access(int *sp)
{
  bool is_safe = validate_address(sp) && validate_address(sp + 1) && validate_address(sp + 2) && validate_address(sp + 3);
  if (!is_safe)
    exit(-1);
}

struct file_desc_mapper *get_file_from_fd(int fd)
{
  struct thread *curr = thread_current();
  struct file_desc_mapper *fdm = NULL;
  struct list_elem *l;

  if (list_empty(&curr->file_desc_list))
    return NULL;
  for (l = list_begin(&curr->file_desc_list); l != list_end(&curr->file_desc_list); l = list_next(l))
  {
    fdm = list_entry(l, struct file_desc_mapper, elem);
    if (fd == fdm->fd)
      break;
  }
  if (fd == fdm->fd)
    return (fdm);
  return (NULL);
}

void exit(int status)
{
  struct thread *curr = thread_current();
  struct thread *parent = curr->parent;
  struct child_exit_status *exiting_child;
  struct file_desc_mapper *fdmap = NULL;
  struct list_elem *l;

  printf("%s: exit(%d)\n", curr->name, status);

  if (parent != NULL)
  {
    exiting_child = (struct child_exit_status *)malloc(sizeof(struct child_exit_status));
    exiting_child->tid = curr->tid;
    exiting_child->exit_status = status;
    list_push_back(&parent->child_list, &exiting_child->elem);

    if (!parent->exec_called)
      sema_up(&curr->parent->parent_sema);
  }

  /* Close all associated file descriptors of the exiting process */
  while(!list_empty(&curr->file_desc_list)) 
  {
    l = list_pop_front(&curr->file_desc_list);
    fdmap = list_entry(l, struct file_desc_mapper, elem);
    file_close(fdmap->exe);
    list_remove(l);
    free(fdmap);
  } 

  /* Free all memory allocated to dead children */
  while (!list_empty(&curr->child_list))
  {
    l = list_pop_front(&curr->child_list);
    exiting_child = list_entry(l, struct child_exit_status, elem);
    list_remove(l);
    free(exiting_child);
  }
  
  /* Close file if open */
  if (curr->exe)
    file_close(curr->exe);
  
  thread_exit();
}

static void halt(void)
{
  shutdown_power_off();
}

static pid_t exec(const char *file)
{
  /*if (!validate_address((void *)file))
    exit(-1);*/
  pid_t pid = -1;
  struct thread *curr = thread_current();
  curr->exec_called = true;
  pid = process_execute(file); //this will call a sema_up() on load() increasing the initial value of 0 to 1.
  sema_down(&curr->parent_sema);
  curr->exec_called = false;
  return ((curr->exec_success) ? pid: -1);
}

static int wait(pid_t pid)
{
  return (process_wait(pid));
}

 static bool create(const char *file, unsigned initial_size)
{
  if (!validate_address((void *)file) || file == NULL)
    exit(-1);
  lock_acquire(&file_lock);
  bool is_created = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return (is_created); 
}

 static bool remove(const char *file)
{
  if (!validate_address((void *)file) || file == NULL)
    exit(-1);
  lock_acquire(&file_lock);
  bool is_removed = filesys_remove(file);
  lock_release(&file_lock);
  return (is_removed);
}

static int open(const char *file)
{
  if (!validate_address((void *)file))
    exit(-1);

  if (file == NULL) /* if no file name is provided */
    return -1;
  
  struct thread *curr = thread_current();
  struct file_desc_mapper *fdm = (struct file_desc_mapper *)malloc(sizeof(struct file_desc_mapper));
  lock_acquire(&file_lock);
  fdm->exe = filesys_open(file);
  lock_release(&file_lock);
  
  if (fdm->exe == NULL)
    return -1;
  
  if (list_empty(&curr->file_desc_list))
    fdm->fd = 2;
  else
    fdm->fd = list_entry(list_back(&curr->file_desc_list), struct file_desc_mapper, elem)->fd + 1;
  list_push_back(&curr->file_desc_list, &fdm->elem);
  return (fdm->fd);
}

static int filesize(int fd)
{
  struct file_desc_mapper *fdm = get_file_from_fd(fd);
  return ((fdm != NULL)? file_length(fdm->exe): -1);
}

 static int read(int fd, void *buffer, unsigned size)
{
  if (buffer == NULL || !validate_address((void *) buffer))
    exit(-1);
  
  off_t bytes_read = -1;
  if (fd == 0)
    bytes_read = input_getc();
  else
  {
    struct file_desc_mapper *fdm = get_file_from_fd(fd);
    if (fdm != NULL)
      bytes_read = file_read(fdm->exe, buffer, size); 
  }
  return (bytes_read);
}

static int write(int fd, void *buffer, unsigned size)
{
  if (buffer == NULL || !validate_address((void *) buffer))
    exit(-1);

  off_t bytes_written = 0;
  if (fd == 1) //write to System console
  {
    putbuf(buffer, size);
    bytes_written = 1;
  }
  else
  {
    struct file_desc_mapper *fdm = get_file_from_fd(fd);
    if (fdm != NULL)
      bytes_written = file_write(fdm->exe, buffer, size);
  }
  return (bytes_written);
}

static void seek(int fd, unsigned position)
{
  struct file_desc_mapper *fdm = get_file_from_fd(fd);
  if (fdm != NULL)
    file_seek(fdm->exe, (off_t)position);
}

static unsigned tell(int fd)
{
  off_t position = -1;
  struct file_desc_mapper *fdm = get_file_from_fd(fd);
  if (fdm != NULL)
    position = file_tell(fdm->exe);
  return (position);
}

static void close(int fd)
{
  struct file_desc_mapper *fdm = get_file_from_fd(fd);
  if (fdm != NULL)
  {
    lock_acquire(&file_lock);
    file_close(fdm->exe);
    list_remove(&fdm->elem);
    free(fdm);
    lock_release(&file_lock);
  }
}
