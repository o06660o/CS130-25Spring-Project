#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "vm/swap.h"
#include <hash.h>

/* Types of a page, see comments of PAGE for more detail. */
enum page_type
{
  PAGE_UNALLOC, /* Unallocated anonymous page. */
  PAGE_ALLOC,   /* Allocated anonymous page. */
  PAGE_FILE     /* File backed page. */
};

/* Page is a kind of abstraction to the memory a user program uses.

   A page can be one of difference types:
   - UNALLOC: unallocated anonymous page. it will be allocated when accessed.
   - ANON: allocated anonymous page. it is not backed by any file. it might be
     in the swap slot if evicted.
   - FILE: file backed page. it might be in the filesys if evicted or unloaded.
 */
struct page
{
  enum page_type type; /* Page type. */
  void *kpage;         /* Frame mapped to this page, if it exists. */
  slot_id slot_idx;    /* Slot store this page, if it exists. */

  /* We need some metadata to load the correct data from file. */
  struct file *file;
  off_t ofs;
  void *upage;
  uint32_t read_bytes;
  uint32_t zero_bytes;
  bool writable;

  struct hash_elem hashelem; /* Used by sup_hash_table. */
  struct list_elem listelem; /* Used by page_list in struct thread. */
  struct thread *owner;      /* The thread that own this page. */
};

void page_init (void);
bool page_lazy_load_anon (struct file *file, off_t ofs, void *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
bool page_lazy_load_file (void);
bool page_full_load (void *fault_addr);
void page_free (struct page *);

#endif /* vm/page.h */
