#include "vm/frame.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include <debug.h>
#include <stdint.h>

/* Frame table. */
static struct lock frame_lock;      /* A lock to protect frame table. */
static struct hash frame_hash;      /* A hash table to store frames. */
static struct list frame_list;      /* A list to evict frames. */
static struct list_elem *clock_ptr; /* Still used to evict frames. */

/* Try to find a frame to evict */
static void frame_evict (void);
/* Helper functions for list. */
static struct list_elem *next_frame_ (void);
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

  if (kpage == NULL)
    return;
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
  if (clock_ptr == &frame->listelem)
    clock_ptr = next_frame_ ();
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

  bool lock_already_held = lock_held_by_current_thread (&frame_lock);
  if (!lock_already_held)
    lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = kpage;
  struct hash_elem *elem = hash_find (&frame_hash, &tmp.hashelem);
  ASSERT (elem != NULL);
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  frame->pinned = status;
  if (!lock_already_held)
    lock_release (&frame_lock);
}

/* Returns the frame after clock_ptr in frame_list. */
static struct list_elem *
next_frame_ (void)
{
  if (clock_ptr == list_end (&frame_list)
      || list_next (clock_ptr) == list_end (&frame_list))
    return list_begin (&frame_list);
  else
    return list_next (clock_ptr);
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
      clock_ptr = next_frame_ ();

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

  /* Pages modified since load should be written to swap; while unmodified
     pages should never be written to swap. */
  ASSERT (victim->upage == victim->sup_page->upage);
  if (victim->sup_page->type == PAGE_ALLOC)
    {
      if (pagedir_is_dirty (victim->owner->pagedir, victim->upage))
        {
          /* According to pintos document, we can panic if the swap is full. */
          slot_id slot_idx = swap_out (victim->kpage);
          if (slot_idx == SLOT_ERR)
            PANIC ("swap is full");
          victim->sup_page->slot_idx = slot_idx;
        }
      else
        {
          victim->sup_page->type = PAGE_UNALLOC;
          victim->sup_page->slot_idx = SLOT_ERR;
        }
    }
  else
    {
      ASSERT (victim->sup_page->type == PAGE_FILE);
      if (pagedir_is_dirty (victim->owner->pagedir, victim->upage))
        {
          if (lock_held_by_current_thread (&filesys_lock))
            file_write_at (victim->sup_page->file, victim->kpage,
                           victim->sup_page->read_bytes,
                           victim->sup_page->ofs);
          else if (lock_try_acquire (&filesys_lock))
            {
              ASSERT (lock_held_by_current_thread (&filesys_lock))
              file_write_at (victim->sup_page->file, victim->kpage,
                             victim->sup_page->read_bytes,
                             victim->sup_page->ofs);
              lock_release (&filesys_lock);
            }
          else
            {
              /* To avoid deadlock, we need to release frame_lock first.
                 Before release the frame_lock, we need to ensure that
                 victim frame won't be evicted again. */
              frame_set_pinned (victim->kpage, true);
              lock_release (&frame_lock);
              lock_acquire (&filesys_lock);
              file_write_at (victim->sup_page->file, victim->kpage,
                             victim->sup_page->read_bytes,
                             victim->sup_page->ofs);
              lock_release (&filesys_lock);
              lock_acquire (&frame_lock);
              frame_set_pinned (victim->kpage, false);
            }
          pagedir_set_dirty (victim->owner->pagedir, victim->upage, false);
        }
    }

  /* Unbound the page from the frame. */
  pagedir_clear_page (victim->owner->pagedir, victim->upage);
  victim->sup_page->kpage = NULL;
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
