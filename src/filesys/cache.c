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
  struct rwlock rwlock;            /* Read-Write lock for block access. */
};

static struct cache_block cache[CACHE_SIZE]; /* Cache blocks. */
static int clock_ptr;                /* Pointer for the clock algorithm. */
static struct lock cache_clock_lock; /* Lock for clock algorithm. */

/* Initializes the buffer cache. */
void
cache_init (void)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  lock_init (&cache_clock_lock);
  clock_ptr = 0;
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      rwlock_init (&cache[i].rwlock);
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
  lock_acquire (&cache_clock_lock);
  struct cache_block *block = NULL;
  while (true)
    {
      block = &cache[clock_ptr];
      ++clock_ptr;
      if (clock_ptr >= CACHE_SIZE)
        clock_ptr = 0;

      rwlock_acquire_writer (&block->rwlock);
      if (!block->valid || !block->accessed)
        break;
      block->accessed = false;
      rwlock_release (&block->rwlock);
    }
  lock_release (&cache_clock_lock);
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
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      rwlock_acquire_reader (&cache[i].rwlock);
      if (cache[i].valid && cache[i].sector == sector)
        {
          memcpy (buffer, cache[i].data + offset, size);
          cache[i].accessed = true;
          rwlock_release (&cache[i].rwlock);
          return;
        }
      rwlock_release (&cache[i].rwlock);
    }
  struct cache_block *block = cache_evict ();
  block->sector = sector;
  block_read (fs_device, sector, block->data);
  memcpy (buffer, block->data + offset, size);
  block->dirty = false;
  block->accessed = false;
  block->valid = true;
  rwlock_release (&block->rwlock);
}

/* Writes a block to the cache. */
void
cache_write (block_sector_t sector, const void *buffer, off_t size,
             off_t offset)
{
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      rwlock_acquire_writer (&cache[i].rwlock);
      if (cache[i].valid && cache[i].sector == sector)
        {
          memcpy (cache[i].data + offset, buffer, size);
          cache[i].dirty = true;
          cache[i].accessed = true;
          rwlock_release (&cache[i].rwlock);
          return;
        }
      rwlock_release (&cache[i].rwlock);
    }
  struct cache_block *block = cache_evict ();
  block->sector = sector;
  block_read (fs_device, sector, block->data);
  memcpy (block->data + offset, buffer, size);
  block->dirty = true;
  block->accessed = false;
  block->valid = true;
  rwlock_release (&block->rwlock);
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