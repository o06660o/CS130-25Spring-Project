#include "filesys/cache.h"
#include "devices/block.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include <debug.h>
#include <stdbool.h>
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
};

/* Free map and root inode are cached permanently. */
static struct cache_block cache[CACHE_SIZE + 2];
static struct lock cache_lock; /* Lock for cache blocks. */
static int clock_ptr;          /* Pointer for the clock algorithm. */

/* Initializes the buffer cache. */
void
cache_init (void)
{
  lock_init (&cache_lock);
  clock_ptr = 0;
  for (int i = 0; i < CACHE_SIZE; i++)
    cache[i].valid = false;
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
      clock_ptr++;
      if (clock_ptr >= CACHE_SIZE)
        clock_ptr = 0;

      if (cb->valid
          && (cb->sector == FREE_MAP_SECTOR || cb->sector == ROOT_DIR_SECTOR))
        continue;
      if (!cb->valid || !cb->accessed)
        break;
      cb->accessed = false;
    }
  if (cb->valid && cb->dirty)
    {
      block_write (cb->block, cb->sector, cb->data);
      cb->dirty = false;
    }
  cb->valid = false;
  return cb;
}

void
cache_read (struct block *block, block_sector_t sector, void *buffer)
{
  struct cache_block *cb = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        cb = &cache[i];

      if (cb != NULL)
        break;
    }
  if (cb != NULL) /* Cache hit. */
    cb->accessed = true;
  else /* Cache miss. */
    {
      cb = cache_evict ();
      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = false;
      cb->accessed = false;
      cb->valid = true;
    }
  memcpy (buffer, cb->data, BLOCK_SECTOR_SIZE);
  lock_release (&cache_lock);
}

void
cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
  struct cache_block *cb = NULL;
  lock_acquire (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].valid && cache[i].block == block
          && cache[i].sector == sector)
        cb = &cache[i];

      if (cb != NULL)
        break;
    }
  if (cb != NULL) /* Cache hit. */
    {
      cb->dirty = true;
      cb->accessed = true;
    }
  else /* Cache miss. */
    {
      cb = cache_evict ();
      cb->block = block;
      cb->sector = sector;
      block_read (block, sector, cb->data);
      cb->dirty = true;
      cb->accessed = false;
      cb->valid = true;
    }
  memcpy (cb->data, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&cache_lock);
}

/* Writes all dirty block back to disk. Must be called with interrupt off. */
void
cache_flush (void)
{
  for (int i = 0; i < CACHE_SIZE; i++)
    if (cache[i].valid && cache[i].dirty)
      {
        block_write (cache[i].block, cache[i].sector, cache[i].data);
        cache[i].dirty = false;
      }
}
