#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <stdint.h>

#define FRAME_DEFAULT 1024u /* Default and minimal size of frame table. */

/* frame table. */

static struct hash frame_hash;
static struct list frame_list;
static struct list_elem *clock_ptr;
static size_t frame_count;
static struct lock frame_lock;

/* Helper functions for hash table. */
static unsigned hash_func (const struct hash_elem *, void *UNUSED);
static bool hash_less (const struct hash_elem *, const struct hash_elem *,
                       void *UNUSED);

/* Initializes the frame table. */
void
frame_init (void)
{
  frame_count = 0;
  hash_init (&frame_hash, hash_func, hash_less, NULL);
  list_init (&frame_list);
  clock_ptr = NULL;
  lock_init (&frame_lock);
}

/* Allocates a frame from user pool, returns its kernel virtual address.
   PAL_USER is automatically set.*/
void *
frame_alloc (enum palloc_flags flags, void *upage)
{
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  lock_acquire (&frame_lock);
  void *kpage = palloc_get_page (flags | PAL_USER);
  if (kpage == NULL)
    {
      // TODO: eviction
      PANIC ("To be implemented.");
    }
  struct frame *frame = (struct frame *)malloc (sizeof (struct frame));
  frame->kpage = kpage;
  frame->upage = upage;
  frame->owner = thread_current ();
  frame->pinned = false;
  hash_insert (&frame_hash, &frame->hashelem);
  list_push_back (&frame_list, &frame->listelem);
  lock_release (&frame_lock);
  return kpage;
}

/* Free a frame at KPAGE. If KPAGE doesn't exist, do nothing. */
void
frame_free (void *kpage)
{
  ASSERT (pg_ofs (kpage) == 0);

  lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = kpage;
  struct hash_elem *elem = hash_delete (&frame_hash, &tmp.hashelem);
  if (elem == NULL)
    {
      lock_release (&frame_lock);
      return;
    }
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  palloc_free_page (frame->kpage);
  list_remove (&frame->listelem);
  free (frame);
  lock_release (&frame_lock);
}

static unsigned
hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct frame *frame = hash_entry (elem, struct frame, hashelem);
  return hash_bytes (&frame->kpage, sizeof (frame->kpage));
}

static bool
hash_less (const struct hash_elem *lhs, const struct hash_elem *rhs,
           void *aux UNUSED)
{
  const struct frame *lhs_ = hash_entry (lhs, struct frame, hashelem);
  const struct frame *rhs_ = hash_entry (rhs, struct frame, hashelem);
  return (uintptr_t)lhs_->kpage < (uintptr_t)rhs_->kpage;
}