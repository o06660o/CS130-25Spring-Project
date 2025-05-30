#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"

/* Number of ticks between cache flushes. */
#define CACHE_FLUSH_FREQ 1000

void cache_init (void);
void cache_read (block_sector_t, void *, off_t, off_t);
void cache_write (block_sector_t, const void *, off_t, off_t);
void cache_flush (void);

#endif /* filesys/cache.h */
