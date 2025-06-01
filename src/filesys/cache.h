#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include "stdbool.h"

/* Number of ticks between cache flushes. */
#define CACHE_FLUSH_FREQ 100

void cache_init (void);
void cache_read (struct block *, block_sector_t, void *, off_t, off_t);
void cache_write (struct block *, block_sector_t, const void *, off_t, off_t);
void cache_flush (bool);
void cache_free (struct block *, block_sector_t);

#endif /* filesys/cache.h */
