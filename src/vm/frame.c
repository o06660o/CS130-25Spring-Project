#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include <debug.h>
#include <stdint.h>

/* frame table. */

static struct hash frame_hash;
static struct list frame_list;
static struct list_elem *clock_ptr;
static struct lock frame_lock;

/* Try to find a frame to evict */
static void frame_evict (void);
/* Helper functions for hash table. */
static unsigned hash_func (const struct hash_elem *, void *UNUSED);
static bool hash_less (const struct hash_elem *, const struct hash_elem *,
                       void *UNUSED);

/* Initializes the frame table. */
void
frame_init (void)
{
  hash_init (&frame_hash, hash_func, hash_less, NULL);
  list_init (&frame_list);
  clock_ptr = list_begin (&frame_list);
  lock_init (&frame_lock);
}

/* Allocates a frame from user pool, returns its kernel virtual address.
   PAL_USER is automatically set.*/
void *
frame_alloc (enum palloc_flags flags, void *upage, struct page *page,
             bool pinned)
{
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  lock_acquire (&frame_lock);
  void *kpage = palloc_get_page (flags | PAL_USER);
  if (kpage == NULL)
    {
      frame_evict ();
      kpage = palloc_get_page (flags | PAL_USER);
    }
  if (kpage == NULL)
    PANIC ("Cannot evict a frame");
  struct frame *frame = (struct frame *)malloc (sizeof (struct frame));
  frame->kpage = kpage;
  frame->upage = upage;
  frame->owner = thread_current ();
  frame->pinned = pinned;
  frame->sup_page = page;
  hash_insert (&frame_hash, &frame->hashelem);
  list_insert (clock_ptr, &frame->listelem);
  lock_release (&frame_lock);
  return kpage;
}

/* Free a frame at KPAGE. If KPAGE doesn't exist, do nothing. */
void
frame_free (void *kpage)
{
  ASSERT (pg_ofs (kpage) == 0);

  bool lock_already_held = lock_held_by_current_thread (&frame_lock);
  if (!lock_already_held)
    lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = kpage;
  struct hash_elem *elem = hash_delete (&frame_hash, &tmp.hashelem);
  if (elem == NULL)
    {
      if (!lock_already_held)
        lock_release (&frame_lock);
      return;
    }
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  palloc_free_page (frame->kpage);
  list_remove (&frame->listelem);
  free (frame);
  if (!lock_already_held)
    lock_release (&frame_lock);
}

/* Change pinned status of frame at KPAGE to STATUS. */
void
frame_set_pinned (void *kpage, bool status)
{
  ASSERT (pg_ofs (kpage) == 0);

  lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = kpage;
  struct hash_elem *elem = hash_find (&frame_hash, &tmp.hashelem);
  if (elem == NULL)
    PANIC ("frame_set_pinned: frame not found");
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  frame->pinned = status;
  lock_release (&frame_lock);
}

/* Evicts a frame from the frame table. */
static void
frame_evict (void)
{
  ASSERT (lock_held_by_current_thread (&frame_lock));

  if (list_empty (&frame_list))
    return;
  struct frame *victim = NULL;
  if (clock_ptr == NULL)
    clock_ptr = list_begin (&frame_list);
  for (int cycle_cnt = 0; cycle_cnt <= 2;)
    {
      if (clock_ptr == list_begin (&frame_list))
        ++cycle_cnt;
      struct frame *f = list_entry (clock_ptr, struct frame, listelem);

      if (clock_ptr == list_end (&frame_list)
          || list_next (clock_ptr) == list_end (&frame_list))
        clock_ptr = list_begin (&frame_list);
      else
        clock_ptr = list_next (clock_ptr);

      if (f->pinned)
        continue;
      if (pagedir_is_accessed (f->owner->pagedir, f->upage))
        {
          pagedir_set_accessed (f->owner->pagedir, f->upage, false);
          continue;
        }
      victim = f;
      break;
    }
  if (victim == NULL)
    return;
  slot_id slot_idx = swap_out (victim->kpage);
  /* According to pintos document, we can panic if the swap is full. */
  ASSERT (slot_idx != SLOT_ERR);

  /* Unbound the page from the frame. */
  pagedir_clear_page (victim->owner->pagedir, victim->upage);
  victim->sup_page->status = PAGE_SWAP;
  victim->sup_page->kpage = NULL;
  victim->sup_page->slot_idx = slot_idx;
  victim->sup_page = NULL;

  /* Free the frame. */
  frame_free (victim->kpage);
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
