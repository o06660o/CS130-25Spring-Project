#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include <hash.h>
#include <stdbool.h>

struct frame
{
  void *kpage;
  void *upage;
  struct thread *owner;
  bool pinned;
  struct hash_elem hashelem;
  struct list_elem listelem;
};

void frame_init (void);
void *frame_alloc (enum palloc_flags, void *);
void frame_free (void *);

#endif /* vm/frame.h */
