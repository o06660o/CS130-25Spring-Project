#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "vm/swap.h"
#include <hash.h>

/* States in a page's life cycle, see comments of PAGE for more detail. */
enum page_status
{
  PAGE_READY,
  PAGE_FRAME,
  PAGE_SWAP
};

/* Page is a kind of abstraction to the memory a user program uses.

   A page can have difference states:
   - READY: since we lazy loads pages, memory is not actually allocated when
     we ask for a page.
   - FRAME: this page is mapped to a physical frame
   - SWAP: when we cannot allocate more frames, we have to evict a page and
     transfer its data to a swap slot.
 */
struct page
{
  enum page_status status; /* Page status. */
  void *kpage;      /* If status is FRAME, we map this page to a frame. */
  slot_id slot_idx; /* If status if SWAP, we map this page to a swap slot. */

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
bool page_lazy_load (struct file *file, off_t ofs, void *upage,
                     uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool page_full_load (void *fault_addr);
void page_free (struct page *);

#endif /* vm/page.h */
