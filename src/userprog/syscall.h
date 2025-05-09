#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <hash.h>
#include <lib/user/syscall.h>
#include <threads/thread.h>

#define OPEN_FILE_MAX 1024 /* Files open limit. */

struct mmap_data
{
  mapid_t mapping;
  struct file *file;
  struct hash_elem hashelem;
  tid_t owner;
  void *uaddr;
};

void syscall_init (void);
void syscall_munmap (mapid_t mapping);

#endif /* userprog/syscall.h */
