#include "vm/page.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include <debug.h>
#include <hash.h>
#include <stdio.h>
#include <string.h>

/* The supplemental page table. */
static struct hash sup_page_table;

/* The lock for the supplemental page table. */
static struct lock sup_page_table_lock;

/* Helper functions for hash table. */
static unsigned hash_func (const struct hash_elem *, void *UNUSED);
static bool hash_less (const struct hash_elem *, const struct hash_elem *,
                       void *UNUSED);

void
page_init (void)
{
  lock_init (&sup_page_table_lock);
  hash_init (&sup_page_table, hash_func, hash_less, NULL);
}

/* Lazy allocates a page, but do not insert it into page directory. */
bool
page_lazy_load (struct file *file, off_t ofs, void *upage, uint32_t read_bytes,
                uint32_t zero_bytes, bool writable, enum page_type type)
{
  struct page *page = malloc (sizeof (struct page));
  if (page == NULL)
    return false;

  ASSERT (pg_round_down (upage) == upage);

  lock_acquire (&sup_page_table_lock);

  page->type = type;
  page->kpage = NULL;
  page->slot_idx = SLOT_ERR;

  page->file = file;
  page->ofs = ofs;
  page->upage = upage;
  page->read_bytes = read_bytes;
  page->zero_bytes = zero_bytes;
  page->writable = writable;

  page->owner = thread_current ();

  hash_insert (&sup_page_table, &page->hashelem);
  if (type == PAGE_UNALLOC)
    list_push_back (&page->owner->page_list, &page->listelem);
  lock_release (&sup_page_table_lock);
  return true;
}

/* Converts a fault address to a page. */
struct page *
get_page (const void *fault_addr, const struct thread *t)
{
  struct page tmp;
  tmp.upage = pg_round_down (fault_addr);
  tmp.owner = (struct thread *)t;
  lock_acquire (&sup_page_table_lock);
  struct hash_elem *e = hash_find (&sup_page_table, &tmp.hashelem);
  lock_release (&sup_page_table_lock);
  return e != NULL ? hash_entry (e, struct page, hashelem) : NULL;
}

/* Fully loads the page and inserts it into the page directory. */
bool
page_full_load (void *fault_addr)
{
  if (fault_addr == NULL)
    return false;

  struct page *page = get_page (fault_addr, thread_current ());
  void *kpage = NULL;
  if (page == NULL)
    goto fail;
  ASSERT (page->owner == thread_current ());

  if (page->type == PAGE_UNALLOC || page->type == PAGE_FILE)
    {
      ASSERT (page->kpage == NULL);
      ASSERT (page->slot_idx == SLOT_ERR);

      /* Try to find a existing frame to share. */
      lock_acquire (&sup_page_table_lock);
      if (page->type == PAGE_UNALLOC && page->file != NULL
          && page->writable == false)
        {
          struct hash_iterator it;
          hash_first (&it, &sup_page_table);
          while (hash_next (&it))
            {
              struct page *cur
                  = hash_entry (hash_cur (&it), struct page, hashelem);
              if (strcmp (page->owner->name, cur->owner->name) == 0
                  && page->ofs == cur->ofs && cur->type == PAGE_ALLOC
                  && cur->writable == false)
                {
                  ASSERT (cur->kpage != NULL);
                  ASSERT (cur->slot_idx == SLOT_ERR);

                  frame_share (cur->kpage, page->upage, page);
                  kpage = cur->kpage;
                  lock_release (&sup_page_table_lock);
                  goto end;
                }
            }
        }
      lock_release (&sup_page_table_lock);

      kpage = frame_alloc (PAL_USER, page->upage, page, true);
      if (kpage == NULL)
        goto fail;

      if (page->read_bytes != 0)
        {
          file_seek (page->file, page->ofs);
          off_t read = file_read (page->file, kpage, page->read_bytes);
          if (read != (off_t)page->read_bytes)
            goto fail;
        }

      memset (kpage + page->read_bytes, 0, page->zero_bytes);
    }
  else if (page->type == PAGE_ALLOC)
    {
      ASSERT (page->kpage == NULL);
      ASSERT (page->slot_idx != SLOT_ERR);
      kpage = frame_alloc (PAL_USER, page->upage, page, true);
      if (kpage == NULL)
        goto fail;

      if (!swap_in (page->slot_idx, kpage))
        goto fail;
    }

end:
  if (pg_ofs (page->upage) != 0)
    printf ("page_full_load: page->upage is not page aligned\n");
  if (!pagedir_install_page (page->owner, page->upage, kpage, page->writable))
    goto fail;
  ASSERT (pagedir_get_page (page->owner->pagedir, page->upage) == kpage);
  /* Remember to recover the dirty bit. */
  if (page->type == PAGE_ALLOC)
    pagedir_set_dirty (page->owner->pagedir, page->upage, true);

  page->kpage = kpage;
  if (page->type == PAGE_UNALLOC)
    page->type = PAGE_ALLOC;
  frame_set_pinned (kpage, false);
  return true;

fail:
  if (kpage != NULL)
    frame_free (kpage);
  return false;
}

void
page_free (struct page *page)
{
  if (page == NULL)
    return;

  if (page->kpage != NULL)
    frame_set_pinned (page->kpage, true);

  if (page->type == PAGE_ALLOC || page->type == PAGE_FILE)
    {
      if (page->kpage != NULL)
        {
          frame_remove (page);
          page->kpage = NULL;
          pagedir_set_accessed (page->owner->pagedir, page->upage, false);
          pagedir_set_dirty (page->owner->pagedir, page->upage, false);
          pagedir_clear_page (page->owner->pagedir, page->upage);
        }
      else
        {
          ASSERT (page->slot_idx != SLOT_ERR);
          void *tmp = palloc_get_page (0);
          /* XXX: will we fail to allocate memory from the kernel pool? */
          ASSERT (tmp != NULL);
          ASSERT (swap_in (page->slot_idx, tmp));
          palloc_free_page (tmp);
        }
    }

  if (page->kpage != NULL)
    frame_set_pinned (page->kpage, false);

  if (page->type != PAGE_FILE)
    list_remove (&page->listelem);
  lock_acquire (&sup_page_table_lock);
  hash_delete (&sup_page_table, &page->hashelem);
  lock_release (&sup_page_table_lock);
  free (page);
}

/* Fully loads a stack page and inserts it into the page directory.
   It will be initialized as an anonymous page and zeroed. */
bool
page_full_load_stack (void *upage)
{
  struct page *page = malloc (sizeof (struct page));
  void *kpage = NULL;
  if (page == NULL)
    return false;

  ASSERT (pg_round_down (upage) == upage);

  lock_acquire (&sup_page_table_lock);
  page->type = PAGE_UNALLOC;
  page->kpage = NULL;
  page->slot_idx = SLOT_ERR;

  page->file = NULL;
  page->ofs = 0;
  page->upage = upage;
  page->read_bytes = 0;
  page->zero_bytes = PGSIZE;
  page->writable = true;

  page->owner = thread_current ();
  hash_insert (&sup_page_table, &page->hashelem);
  list_push_back (&page->owner->page_list, &page->listelem);
  lock_release (&sup_page_table_lock);

  kpage = frame_alloc (PAL_USER | PAL_ZERO, upage, page, true);
  if (kpage == NULL)
    goto fail;

  if (!pagedir_install_page (page->owner, page->upage, kpage, page->writable))
    goto fail;
  ASSERT (pagedir_get_page (page->owner->pagedir, page->upage) == kpage);

  page->kpage = kpage;
  page->type = PAGE_ALLOC;
  frame_set_pinned (kpage, false);
  return true;
fail:
  free (page);
  if (kpage != NULL)
    frame_free (kpage);
  return false;
}

static unsigned
hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *page = hash_entry (elem, struct page, hashelem);
  unsigned h1 = hash_bytes (&page->upage, sizeof (page->upage));
  unsigned h2 = hash_int (page->owner == NULL ? TID_ERROR : page->owner->tid);
  return h1 ^ h2;
}

static bool
hash_less (const struct hash_elem *lhs, const struct hash_elem *rhs,
           void *aux UNUSED)
{
  const struct page *lhs_ = hash_entry (lhs, struct page, hashelem);
  const struct page *rhs_ = hash_entry (rhs, struct page, hashelem);
  uintptr_t l_upage = (uintptr_t)lhs_->upage;
  tid_t l_tid = lhs_->owner == NULL ? TID_ERROR : lhs_->owner->tid;
  uintptr_t r_upage = (uintptr_t)rhs_->upage;
  tid_t r_tid = rhs_->owner == NULL ? TID_ERROR : rhs_->owner->tid;
  return l_upage < r_upage || (l_upage == r_upage && l_tid < r_tid);
}
