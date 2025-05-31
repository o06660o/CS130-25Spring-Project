#include "filesys/cache.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include <debug.h>
#include <stdbool.h>
#include <string.h>

#define CACHE_SIZE 64

/* Partition that contains the file system. */
struct block *fs_device;

/* Cache a sector of disk storage. */
struct cache_block
{
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

/* Initializes the buffer cache. */
void
cache_init (void)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  lock_init (&cache_lock);
  clock_ptr = 0;
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_init (&cache[i].lock);
      cache[i].valid = false;
      cache[i].dirty = false;
    }
}

/* Evicts a cache block, writing it back to disk if dirty.
   The rwlock of evicted block will be released in cache_read() or
   cache_write(). */
static struct cache_block *
cache_evict (void)
{
  struct cache_block *block = NULL;
  while (true)
    {
      block = &cache[clock_ptr];
      ++clock_ptr;
      if (clock_ptr >= CACHE_SIZE)
        clock_ptr = 0;

      lock_acquire (&block->lock);
      if (!block->valid || !block->accessed)
        break;
      block->accessed = false;
      lock_release (&block->lock);
    }
  if (block->dirty)
    {
      block_write (fs_device, block->sector, block->data);
      block->dirty = false;
    }
  block->valid = false;
  return block;
}

/* Reads a block from the cache. If the block is not in the cache, read it
   from disk and add it to the cache. */
void
cache_read (block_sector_t sector, void *buffer, off_t size, off_t offset)
{
  struct cache_block *block = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].sector == sector)
        {
          block = &cache[i];
          block->accessed = true;
          break;
        }
      lock_release (&cache[i].lock);
    }
  if (block == NULL)
    {
      block = cache_evict ();
      lock_release (&cache_lock);

      /* Release the lock before waiting for IO. */
      block->sector = sector;
      block_read (fs_device, sector, block->data);
      block->dirty = false;
      block->accessed = false;
      block->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (buffer, block->data + offset, size);
  lock_release (&block->lock);
}

/* Writes a block to the cache. */
void
cache_write (block_sector_t sector, const void *buffer, off_t size,
             off_t offset)
{
  struct cache_block *block = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      lock_acquire (&cache[i].lock);
      if (cache[i].valid && cache[i].sector == sector)
        {
          block = &cache[i];
          block->dirty = true;
          block->accessed = true;
          break;
        }
      lock_release (&cache[i].lock);
    }
  if (block == NULL)
    {
      block = cache_evict ();
      lock_release (&cache_lock);

      /* Release the lock before waiting for IO. */
      block->sector = sector;
      block_read (fs_device, sector, block->data);
      block->dirty = true;
      block->accessed = false;
      block->valid = true;
    }
  else
    lock_release (&cache_lock);

  memcpy (block->data + offset, buffer, size);
  lock_release (&block->lock);
}

/* Writes all dirty block back to disk. Must be called with interrupt off. */
void
cache_flush (void)
{
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      if (cache[i].valid && cache[i].dirty)
        {
          block_write (fs_device, cache[i].sector, cache[i].data);
          cache[i].dirty = false;
        }
    }
}
