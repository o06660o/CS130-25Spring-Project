#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"

/* Number of ticks between cache flushes. */
#define CACHE_FLUSH_FREQ 1000

void cache_init (void);
void cache_read (struct block *, block_sector_t, void *);
void cache_write (struct block *, block_sector_t, const void *);
void cache_flush (void);

#endif /* filesys/cache.h */
