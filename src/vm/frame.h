#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "vm/page.h"
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
  struct page *sup_page;
};

void frame_init (void);
void *frame_alloc (enum palloc_flags, void *, struct page *);
void frame_free (void *);

#endif /* vm/frame.h */
