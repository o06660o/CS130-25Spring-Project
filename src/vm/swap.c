#include "vm/swap.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include <debug.h>

/* The swap partition. */
static struct block *swap_device;

/* We use a bitmap to track allocated slots. */
static struct bitmap *swap_bitmap;

/* Use a lock to protect the swap bitmap. */
static struct lock swap_lock;

/* The number of sectors a slot occupies. */
#define SLOT_SIZE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Initializes the swap table. */
void
swap_init (void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("The swap partition is unavailable, can't initialize swap table.");

  swap_bitmap = bitmap_create (block_size (swap_device) / SLOT_SIZE);
  if (swap_bitmap == NULL)
    PANIC ("Swap bitmap creation failed.");
  bitmap_set_all (swap_bitmap, false);

  lock_init (&swap_lock);
}

/* If the swap partition is not full, try to allocate a swap slot and return
   its index. Otherwise, return SLOT_ERR. */
slot_id
swap_out (const void *kpage)
{
  lock_acquire (&swap_lock);
  slot_id ret = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);
  lock_release (&swap_lock);
  if (ret == BITMAP_ERROR)
    return SLOT_ERR;

  for (int i = 0; i < SLOT_SIZE; i++)
    block_write (swap_device, ret * SLOT_SIZE + i,
                 kpage + i * BLOCK_SECTOR_SIZE);
  return ret;
}

/* If the slot is valid, transter the data from the swap partition to the given
   memory address and returns true. Otherwise, returns false.

   It is the caller's responsibility to ensure that the memory address given is
   valid and has enough space. */
bool
swap_in (slot_id swap_idx, void *kpage)
{
  lock_acquire (&swap_lock);
  bool failed = false;
  if (swap_idx >= bitmap_size (swap_bitmap))
    failed = true;
  if (bitmap_test (swap_bitmap, swap_idx) == false)
    failed = true;
  lock_release (&swap_lock);
  if (failed)
    return false;

  for (int i = 0; i < SLOT_SIZE; i++)
    block_read (swap_device, swap_idx * SLOT_SIZE + i,
                kpage + i * BLOCK_SECTOR_SIZE);

  lock_acquire (&swap_lock);
  bitmap_set (swap_bitmap, swap_idx, false);
  lock_release (&swap_lock);
  return true;
}
