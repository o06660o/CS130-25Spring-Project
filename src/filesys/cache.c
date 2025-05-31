#include "filesys/cache.h"
#include "devices/block.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <list.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CACHE_SIZE 64

/* Cache a sector of disk storage. */
struct cache_block
{
  struct block *block;             /* Pointer to the block device. */
  block_sector_t sector;           /* Sector number of the block. */
  bool dirty;                      /* true if block dirty, false otherwise. */
  bool valid;                      /* true if block valid, false otherwise. */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Data stored in the block. */
  struct lock lock;      /* A more fine-grained lock for this block. */
  struct list_elem elem; /* List element for the cache list. */
};

static struct cache_block cache[CACHE_SIZE]; /* Cache blocks. */
static struct list cache_list; /* cache list, sorted by reference time. */
static struct lock cache_lock; /* Lock for LRU algorithm. */

static bool flush_done = false; /* If flush thread should stop. */

static void flush_func (void *aux);

/* Initializes the buffer cache. */
void
cache_init (void)
{
  lock_init (&cache_lock);
  list_init (&cache_list);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_init (&cache[i].lock);
      cache[i].valid = false;
      cache[i].dirty = false;
      list_push_back (&cache_list, &cache[i].elem);
    }
  thread_create ("cache flush", PRI_DEFAULT, flush_func, NULL);
}

/* Reads a block from the cache. If the block is not in the cache, read it
   from disk and add it to the cache. */
void
cache_read (struct block *block, block_sector_t sector, void *buffer,
            off_t size, off_t offset)
{
  ASSERT (sector != BLOCK_SECTOR_NONE);
  struct cache_block *cb = NULL;
  bool is_cache_block_lock_held = false;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      bool is_current_cache_block_lock_held
          = lock_held_by_current_thread (&cache[i].lock);
      if (!is_current_cache_block_lock_held)
        lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        {
          cb = &cache[i];
          list_remove (&cb->elem);
          list_push_back (&cache_list, &cb->elem);
          is_cache_block_lock_held = is_current_cache_block_lock_held;
          break;
        }
      if (!is_current_cache_block_lock_held)
        lock_release (&cache[i].lock);
    }
  if (cb == NULL)
    {
      /* Evicts a cache block, writing it back to disk if dirty. */
      cb = list_entry (list_pop_front (&cache_list), struct cache_block, elem);
      is_cache_block_lock_held = lock_held_by_current_thread (&cb->lock);
      if (!is_cache_block_lock_held)
        lock_acquire (&cb->lock);
      list_push_back (&cache_list, &cb->elem);

      /* Release the lock before waiting for IO. */
      lock_release (&cache_lock);

      if (cb->valid && cb->dirty)
        block_write (cb->block, cb->sector, cb->data);

      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = false;
      cb->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (buffer, cb->data + offset, size);
  if (!is_cache_block_lock_held)
    lock_release (&cb->lock);
}

/* Writes a block to the cache. */
void
cache_write (struct block *block, block_sector_t sector, const void *buffer,
             off_t size, off_t offset)
{
  ASSERT (sector != BLOCK_SECTOR_NONE);
  struct cache_block *cb = NULL;
  bool is_cache_block_lock_held = false;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      bool is_current_cache_block_lock_held
          = lock_held_by_current_thread (&cache[i].lock);
      if (!is_current_cache_block_lock_held)
        lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        {
          cb = &cache[i];
          cb->dirty = true;
          list_remove (&cb->elem);
          list_push_back (&cache_list, &cb->elem);
          is_cache_block_lock_held = is_current_cache_block_lock_held;
          break;
        }
      if (!is_current_cache_block_lock_held)
        lock_release (&cache[i].lock);
    }
  if (cb == NULL)
    {
      /* Evicts a cache block, writing it back to disk if dirty. */
      cb = list_entry (list_pop_front (&cache_list), struct cache_block, elem);
      is_cache_block_lock_held = lock_held_by_current_thread (&cb->lock);
      if (!is_cache_block_lock_held)
        lock_acquire (&cb->lock);
      list_push_back (&cache_list, &cb->elem);

      /* Release the lock before waiting for IO. */
      lock_release (&cache_lock);

      if (cb->valid && cb->dirty)
        block_write (cb->block, cb->sector, cb->data);

      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = true;
      cb->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (cb->data + offset, buffer, size);
  if (!is_cache_block_lock_held)
    lock_release (&cb->lock);
}

/* Writes all dirty block back to disk. */
void
cache_flush (bool done)
{
  flush_done = done;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      if (cache[i].valid && cache[i].dirty)
        {
          block_write (cache[i].block, cache[i].sector, cache[i].data);
          cache[i].dirty = false;
        }
    }
  lock_release (&cache_lock);
}

/* We need to create a thread to periodically flush the cache.
   This function is a placeholder for that thread's function. */
static void
flush_func (void *aux UNUSED)
{
  while (!flush_done)
    {
      timer_sleep (CACHE_FLUSH_FREQ);
      cache_flush (false);
    }
}
