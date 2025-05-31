#include "filesys/cache.h"
#include "devices/block.h"
#include "devices/timer.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CACHE_SIZE 64

/* Cache a sector of disk storage. */
struct cache_block
{
  struct block *block;   /* Pointer to the block device. */
  block_sector_t sector; /* Sector number of the block. */
  bool accessed; /* true if block has been accessed, false otherwise. */
  bool dirty;    /* true if block dirty, false otherwise. */
  bool valid;    /* true if block valid, false otherwise. */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Data stored in the block. */
  struct lock lock; /* A more fine-grained lock for this block. */
};

static struct cache_block cache[CACHE_SIZE]; /* Cache blocks. */
static int clock_ptr;          /* Pointer for the clock algorithm. */
static struct lock cache_lock; /* Lock for clock algorithm. */

static bool flush_done = false; /* If flush thread should stop. */

static void flush_func (void *aux);

/* Initializes the buffer cache. */
void
cache_init (void)
{
  lock_init (&cache_lock);
  clock_ptr = 0;
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_init (&cache[i].lock);
      cache[i].valid = false;
      cache[i].dirty = false;
    }
  thread_create ("cache flush", PRI_DEFAULT, flush_func, NULL);
}

/* Evicts a cache block, writing it back to disk if dirty.
   The rwlock of evicted block will be released in cache_read() or
   cache_write(). */
static struct cache_block *
cache_evict (void)
{
  struct cache_block *cb = NULL;
  while (true)
    {
      cb = &cache[clock_ptr];
      ++clock_ptr;
      if (clock_ptr >= CACHE_SIZE)
        clock_ptr = 0;

      lock_acquire (&cb->lock);
      if (!cb->valid || !cb->accessed)
        break;
      cb->accessed = false;
      lock_release (&cb->lock);
    }
  if (cb->dirty)
    {
      block_write (cb->block, cb->sector, cb->data);
      cb->dirty = false;
    }
  cb->valid = false;
  return cb;
}

/* Reads a block from the cache. If the block is not in the cache, read it
   from disk and add it to the cache. */
void
cache_read (struct block *block, block_sector_t sector, void *buffer,
            off_t size, off_t offset)
{
  ASSERT (sector != BLOCK_SECTOR_NONE);
  struct cache_block *cb = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        {
          cb = &cache[i];
          cb->accessed = true;
          break;
        }
      lock_release (&cache[i].lock);
    }
  if (cb == NULL)
    {
      cb = cache_evict ();
      lock_release (&cache_lock);

      /* Release the lock before waiting for IO. */
      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = false;
      cb->accessed = false;
      cb->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (buffer, cb->data + offset, size);
  lock_release (&cb->lock);
}

/* Writes a block to the cache. */
void
cache_write (struct block *block, block_sector_t sector, const void *buffer,
             off_t size, off_t offset)
{
  ASSERT (sector != BLOCK_SECTOR_NONE);
  struct cache_block *cb = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        {
          cb = &cache[i];
          cb->dirty = true;
          cb->accessed = true;
          break;
        }
      lock_release (&cache[i].lock);
    }
  if (cb == NULL)
    {
      cb = cache_evict ();
      lock_release (&cache_lock);

      /* Release the lock before waiting for IO. */
      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = true;
      cb->accessed = false;
      cb->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (cb->data + offset, buffer, size);
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
