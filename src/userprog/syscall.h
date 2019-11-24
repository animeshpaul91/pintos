#include "threads/thread.h"

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//Added File Descriptor Mapper begins
struct file_desc_mapper
{
    int fd;
    struct file *exe;
    struct list_elem elem;
};

//Added Ends
void syscall_init (void);
void exit(int);
typedef int pid_t;

#endif /* userprog/syscall.h */
