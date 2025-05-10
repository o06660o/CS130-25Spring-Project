#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "vm/page.h"
#include <hash.h>
#include <stdbool.h>

struct frame
{
  void *kpage;               /* Address returned by palloc. */
  void *upage;               /* Address returned to user. */
  struct thread *owner;      /* Owner thread of this frame. */
  bool pinned;               /* Whether this frame can be evicted. */
  struct hash_elem hashelem; /* The hash element in frame_hash. */
  struct list_elem listelem; /* The list element in frame_list. */
  struct page *sup_page;     /* The corresponding supplemental page. */
};

void frame_init (void);
void *frame_alloc (enum palloc_flags, void *, struct page *, bool);
void frame_free (void *);
void frame_set_pinned (void *, bool);

#endif /* vm/frame.h */
