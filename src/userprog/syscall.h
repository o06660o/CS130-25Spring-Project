#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <hash.h>
#include <lib/user/syscall.h>
#include <threads/thread.h>

#define OPEN_FILE_MAX 1024 /* Files open limit. */

struct mmap_data
{
  mapid_t mapping;           /* Unique identifier within a process */
  struct file *file;         /* File mapped to this memory segment. */
  struct hash_elem hashelem; /* The hash element in mmap_table. */
  tid_t owner;               /* Owner process of this mapping. */
  void *uaddr;               /* Begin of mapped memory address. */
};

void syscall_init (void);
void syscall_munmap (mapid_t mapping);

#endif /* userprog/syscall.h */
