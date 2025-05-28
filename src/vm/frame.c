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
   PAL_USER is automatically set. */
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
  struct frame_owner *owner
      = (struct frame_owner *)malloc (sizeof (struct frame_owner));
  frame->kpage = kpage;
  frame->pinned = pinned;
  list_init (&frame->owner_list);
  owner->upage = upage;
  owner->thread = thread_current ();
  owner->sup_page = page;
  list_push_back (&frame->owner_list, &owner->listelem);
  hash_insert (&frame_hash, &frame->hashelem);
  list_insert (clock_ptr, &frame->listelem);
  lock_release (&frame_lock);
  return kpage;
}

/* Share the frame at KPAGE with page PAGE. */
void
frame_share (void *kpage, void *upage, struct page *page)
{
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = kpage;
  struct hash_elem *elem = hash_find (&frame_hash, &tmp.hashelem);
  ASSERT (elem != NULL);
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  struct frame_owner *owner
      = (struct frame_owner *)malloc (sizeof (struct frame_owner));
  owner->upage = upage;
  owner->thread = thread_current ();
  owner->sup_page = page;
  list_push_back (&frame->owner_list, &owner->listelem);

#ifdef DEBUG
  printf ("frame_share: kpage = %p\n", kpage);
  struct list_elem *st = list_begin (&frame->owner_list);
  struct list_elem *ed = list_end (&frame->owner_list);
  for (struct list_elem *it = st; it != ed; it = list_next (it))
    {
      struct frame_owner *owner
          = list_entry (it, struct frame_owner, listelem);
      printf ("frame_share: upage = %p, tid = %d\n", owner->upage,
              owner->thread->tid);
    }
  printf ("\n");
#endif

  lock_release (&frame_lock);
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

  ASSERT (list_empty (&frame->owner_list));

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

      struct list_elem *st = list_begin (&f->owner_list);
      struct list_elem *ed = list_end (&f->owner_list);
      bool is_accessed = false;
      for (struct list_elem *it = st; it != ed; it = list_next (it))
        {
          struct frame_owner *owner
              = list_entry (it, struct frame_owner, listelem);
          if (pagedir_is_accessed (owner->thread->pagedir, owner->upage))
            {
              pagedir_set_accessed (owner->thread->pagedir, owner->upage,
                                    false);
              is_accessed = true;
            }
        }
      if (is_accessed)
        continue;

      victim = f;
      break;
    }
  if (victim == NULL)
    return;

  /* Pages modified since load should be written to swap; while unmodified
     pages should never be written to swap. */
  struct frame_owner *victim_owner = list_entry (
      list_begin (&victim->owner_list), struct frame_owner, listelem);
  ASSERT (victim_owner->upage == victim_owner->sup_page->upage);
  if (victim_owner->sup_page->type == PAGE_ALLOC)
    {
      if (pagedir_is_dirty (victim_owner->thread->pagedir,
                            victim_owner->upage))
        {
          /* According to pintos document, we can panic if the swap is full. */
          slot_id slot_idx = swap_out (victim->kpage);
          if (slot_idx == SLOT_ERR)
            PANIC ("swap is full");
          victim_owner->sup_page->slot_idx = slot_idx;
        }
      else
        {
          struct list_elem *st = list_begin (&victim->owner_list);
          struct list_elem *ed = list_end (&victim->owner_list);
          for (struct list_elem *it = st; it != ed; it = list_next (it))
            {
              struct frame_owner *owner
                  = list_entry (it, struct frame_owner, listelem);
              owner->sup_page->type = PAGE_UNALLOC;
              owner->sup_page->slot_idx = SLOT_ERR;
            }
        }
    }
  else
    {
      ASSERT (victim_owner->sup_page->type == PAGE_FILE);
      if (pagedir_is_dirty (victim_owner->thread->pagedir,
                            victim_owner->upage))
        {
          file_write_at (victim_owner->sup_page->file, victim->kpage,
                         victim_owner->sup_page->read_bytes,
                         victim_owner->sup_page->ofs);
          pagedir_set_dirty (victim_owner->thread->pagedir,
                             victim_owner->upage, false);
        }
    }

  /* Unbound the page from the frame. */
  struct list_elem *st = list_begin (&victim->owner_list);
  struct list_elem *ed = list_end (&victim->owner_list);
  for (struct list_elem *it = st; it != ed;)
    {
      struct frame_owner *owner
          = list_entry (it, struct frame_owner, listelem);
      it = list_next (it);
      pagedir_clear_page (owner->thread->pagedir, owner->upage);
      owner->sup_page->kpage = NULL;
      list_remove (&owner->listelem);
      free (owner);
    }
  /* Free the frame. */
  frame_free (victim->kpage);
}

/* Removes the share of page PAGE to frame. If this is the last share, free the
   frame. */
void
frame_remove (struct page *page)
{
  lock_acquire (&frame_lock);
  struct frame tmp;
  tmp.kpage = page->kpage;
  struct hash_elem *elem = hash_find (&frame_hash, &tmp.hashelem);
  ASSERT (elem != NULL);
  struct frame *frame = hash_entry (elem, struct frame, hashelem);
  struct list_elem *st = list_begin (&frame->owner_list);
  struct list_elem *ed = list_end (&frame->owner_list);
  for (struct list_elem *it = st; it != ed;)
    {
      struct frame_owner *owner
          = list_entry (it, struct frame_owner, listelem);
      if (owner->sup_page == page)
        {
          it = list_remove (it);
          free (owner);
          break;
        }
      else
        it = list_next (it);
    }
  if (list_empty (&frame->owner_list))
    frame_free (frame->kpage);
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
