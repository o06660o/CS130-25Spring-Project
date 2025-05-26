#include "filesys/cache.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>

#define CACHE_SIZE 64

struct cache_block
{
  block_sector_t sector;           /* Sector number of the block. */
  bool dirty;                      /* true if block dirty, false otherwise. */
  bool valid;                      /* true if block valid, false otherwise. */
  uint8_t data[BLOCK_SECTOR_SIZE]; /* Data stored in the block. */
  struct lock lock;                /* Lock for block access. */
  struct list_elem elem;           /* List element in cache_list. */
};

static struct cache_block cache[CACHE_SIZE]; /* Cache blocks. */
static struct list cache_list;               /* List of cache blocks. */
static struct lock cache_lock;               /* Lock for cache access. */

void
cache_init (void)
{
  list_init (&cache_list);
  lock_init (&cache_lock);
  for (int i = 0; i < CACHE_SIZE; ++i)
    {
      cache[i].valid = false;
      lock_init (&cache[i].lock);
    }
}