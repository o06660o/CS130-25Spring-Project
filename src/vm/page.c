#include "vm/page.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
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

static struct page *faddr_to_page (const void *fault_addr);

void
page_init (void)
{
  lock_init (&sup_page_table_lock);
  hash_init (&sup_page_table, hash_func, hash_less, NULL);
}

/* Lazy allocates a page, but do not insert it into page directory. */
bool
page_lazy_load (struct file *file, off_t ofs, void *upage, uint32_t read_bytes,
                uint32_t zero_bytes, bool writable)
{
  struct page *page = malloc (sizeof (struct page));
  if (page == NULL)
    return false;

  ASSERT (pg_round_down (upage) == upage);

  lock_acquire (&sup_page_table_lock);
  page->status = PAGE_READY;
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
  list_push_back (&page->owner->page_list, &page->listelem);
  lock_release (&sup_page_table_lock);
  return true;
}

/* Converts a fault address to a page. */
static struct page *
faddr_to_page (const void *fault_addr)
{
  struct page tmp;
  tmp.upage = pg_round_down (fault_addr);
  struct hash_elem *e = hash_find (&sup_page_table, &tmp.hashelem);
  return e != NULL ? hash_entry (e, struct page, hashelem) : NULL;
}

/* Fully loads the page and inserts it into the page directory. */
bool
page_full_load (void *fault_addr)
{
  printf ("[DEBUG]: trying to load page from address %p, ", fault_addr);
  if (fault_addr == NULL)
    return false;

  lock_acquire (&sup_page_table_lock);
  struct page *page = faddr_to_page (fault_addr);
  void *kpage = NULL;
  if (page == NULL)
    goto fail;

  if (page->status == PAGE_READY)
    {
      ASSERT (page->kpage == NULL);
      kpage = frame_alloc (PAL_USER, page->upage);
      if (kpage == NULL)
        goto fail;

      lock_acquire (&filesys_lock);
      file_seek (page->file, page->ofs);
      off_t read = file_read (page->file, kpage, page->read_bytes);
      lock_release (&filesys_lock);
      if (read != (off_t)page->read_bytes)
        goto fail;

      memset (kpage + page->read_bytes, 0, page->zero_bytes);
    }
  else if (page->status == PAGE_SWAP)
    {
      ASSERT (page->kpage == NULL);
      ASSERT (page->slot_idx != SLOT_ERR);
      kpage = frame_alloc (PAL_USER, page->upage);
      if (kpage == NULL)
        goto fail;

      if (!swap_in (page->slot_idx, kpage))
        goto fail;
    }
  else
    PANIC ("page_full_load: page status is not PAGE_READY or PAGE_SWAP");

  ASSERT (kpage != NULL);
  if (!pagedir_install_page (page->owner, page->upage, kpage, page->writable))
    goto fail;

  printf ("page %p is loaded\n", page->upage);

  page->kpage = kpage;
  page->status = PAGE_FRAME;
  lock_release (&sup_page_table_lock);
  return true;

fail:
  ASSERT (page->status != PAGE_FRAME);
  if (kpage != NULL)
    frame_free (kpage);
  lock_release (&sup_page_table_lock);
  return false;
}

void
page_free (struct page *page)
{
  if (page == NULL)
    return;
  if (page->status == PAGE_FRAME)
    {
      frame_free (page->kpage);
      pagedir_clear_page (page->owner->pagedir, page->upage);
      pagedir_set_accessed (page->owner->pagedir, page->upage, false);
      pagedir_set_dirty (page->owner->pagedir, page->upage, false);
    }
  else if (page->status == PAGE_SWAP)
    {
      void *tmp = palloc_get_page (0);
      /* XXX: will we fail to allocate memory from the kernel pool? */
      ASSERT (tmp != NULL);
      ASSERT (swap_in (page->slot_idx, tmp));
      palloc_free_page (tmp);
    }
  hash_delete (&sup_page_table, &page->hashelem);
  free (page);
}

static unsigned
hash_func (const struct hash_elem *elem, void *aux UNUSED)
{
  const struct page *page = hash_entry (elem, struct page, hashelem);
  return hash_bytes (&page->upage, sizeof (page->upage));
}

static bool
hash_less (const struct hash_elem *lhs, const struct hash_elem *rhs,
           void *aux UNUSED)
{
  const struct page *lhs_ = hash_entry (lhs, struct page, hashelem);
  const struct page *rhs_ = hash_entry (rhs, struct page, hashelem);
  return (uintptr_t)lhs_->upage < (uintptr_t)rhs_->upage;
}
