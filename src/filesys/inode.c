#include "filesys/inode.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <string.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static off_t inode_length_unlocked (struct inode *inode);

struct indirect_block
{
  block_sector_t sectors[128]; /* Pointers to data blocks. */
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  int32_t is_dir;                 /* True if this inode is a directory. */
  int32_t file_cnt;               /* Only useful when it is a directory. */
  off_t length;                   /* File size in bytes. */
  block_sector_t parent;          /* Parent directory inode number. */
  block_sector_t direct[10];      /* Direct pointers to data. */
  block_sector_t indirect;        /* Indirect pointer to data. */
  block_sector_t doubly_indirect; /* Doubly indirect pointer to data. */
  unsigned magic;                 /* Magic number. */

  /* Not used. */
  uint8_t unused[BLOCK_SECTOR_SIZE - sizeof (block_sector_t) * 13
                 - sizeof (off_t) - sizeof (unsigned) - sizeof (int32_t) * 2];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
{
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  int deny_write_cnt;     /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
  struct rwlock rwlock;   /* Read-write lock for inode. */
};

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Lock to protect the open_inodes list. */
static struct lock open_inodes_lock;

/* Lock to protect inode reopening.

   inode_reopen() and inode_close() cannot be called simultaneously on the
   same inode.
*/
static struct lock inode_reopen_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
  lock_init (&inode_reopen_lock);
}

/* Helper functions for byte_to_sector_unlocked(). */
static block_sector_t indirect_lookup (const block_sector_t, off_t pos);
static block_sector_t doubly_indirect_lookup (const block_sector_t, off_t pos);

static block_sector_t
indirect_lookup (const block_sector_t sector, off_t pos)
{
  if (sector == BLOCK_SECTOR_NONE)
    return BLOCK_SECTOR_NONE;
  struct indirect_block *ib = malloc (sizeof (struct indirect_block));
  cache_read (fs_device, sector, ib, BLOCK_SECTOR_SIZE, 0);

  off_t idx = pos / BLOCK_SECTOR_SIZE;
  ASSERT (idx < 128);
  block_sector_t ret = ib->sectors[idx];
  free (ib);
  return ret;
}

static block_sector_t
doubly_indirect_lookup (const block_sector_t sector, off_t pos)
{
  if (sector == BLOCK_SECTOR_NONE)
    return BLOCK_SECTOR_NONE;
  struct indirect_block *ib = malloc (sizeof (struct indirect_block));
  cache_read (fs_device, sector, ib, BLOCK_SECTOR_SIZE, 0);

  off_t idx = pos / (BLOCK_SECTOR_SIZE * 128);
  off_t pos_ = pos % (BLOCK_SECTOR_SIZE * 128);
  block_sector_t ret = indirect_lookup (ib->sectors[idx], pos_);
  free (ib);
  return ret;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns BLOCK_SECTOR_NONE if INODE does not contain data for a byte at
   offset POS. */
static block_sector_t
byte_to_sector_unlocked (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  off_t lower = 0, delta = 0;

  delta = 10 * BLOCK_SECTOR_SIZE;
  if (pos < lower + delta)
    return inode->data.direct[pos / BLOCK_SECTOR_SIZE];
  lower += delta;

  delta = 128 * BLOCK_SECTOR_SIZE;
  if (pos < lower + delta)
    return indirect_lookup (inode->data.indirect, pos - lower);
  lower += delta;

  delta = 128 * 128 * BLOCK_SECTOR_SIZE;
  if (pos < lower + delta)
    return doubly_indirect_lookup (inode->data.doubly_indirect, pos - lower);
  lower += delta;

  return BLOCK_SECTOR_NONE; /* This byte cannot be stored in this inode. */
}

static bool
inode_indirect_allocate (block_sector_t *sector)
{
  if (!free_map_allocate (1, sector))
    return false;
  struct indirect_block *ib = malloc (sizeof (struct indirect_block));
  for (int i = 0; i < 128; i++)
    ib->sectors[i] = BLOCK_SECTOR_NONE;
  cache_write (fs_device, *sector, ib, BLOCK_SECTOR_SIZE, 0);
  free (ib);
  return true;
}

static bool
inode_grow_unlocked (struct inode_disk *disk_inode, int sectors)
{
  if (sectors == 0)
    return true;
  static char zeros[BLOCK_SECTOR_SIZE];
  int allocated_sectors = 0;

  /* Direct pointers. */
  for (int i = 0; i < 10 && allocated_sectors < sectors; i++)
    {
      if (disk_inode->direct[i] != BLOCK_SECTOR_NONE)
        continue;

      block_sector_t *sector = &disk_inode->direct[i];
      if (free_map_allocate (1, sector))
        {
          ++allocated_sectors;
          cache_write (fs_device, *sector, zeros, BLOCK_SECTOR_SIZE, 0);
        }
      else
        goto fail;
    }
  if (allocated_sectors >= sectors)
    return true;

  /* Indirect pointer. */
  if (disk_inode->indirect == BLOCK_SECTOR_NONE)
    if (!inode_indirect_allocate (&disk_inode->indirect))
      goto fail;
  {
    struct indirect_block *ib = malloc (sizeof (struct indirect_block));
    cache_read (fs_device, disk_inode->indirect, ib, BLOCK_SECTOR_SIZE, 0);
    for (int i = 0; i < 128 && allocated_sectors < sectors; i++)
      {
        if (ib->sectors[i] != BLOCK_SECTOR_NONE)
          continue;

        if (free_map_allocate (1, &ib->sectors[i]))
          {
            ++allocated_sectors;
            cache_write (fs_device, ib->sectors[i], zeros, BLOCK_SECTOR_SIZE,
                         0);
          }
        else
          {
            cache_write (fs_device, disk_inode->indirect, ib,
                         BLOCK_SECTOR_SIZE, 0);
            free (ib);
            goto fail;
          }
      }
    cache_write (fs_device, disk_inode->indirect, ib, BLOCK_SECTOR_SIZE, 0);
    free (ib);
  }

  if (allocated_sectors >= sectors)
    return true;

  /* Doubly indirect pointer. */
  block_sector_t *doubly_indirect = &disk_inode->doubly_indirect;
  if (*doubly_indirect == BLOCK_SECTOR_NONE)
    if (!inode_indirect_allocate (doubly_indirect))
      goto fail;
  {
    struct indirect_block *dib = malloc (sizeof (struct indirect_block));
    cache_read (fs_device, *doubly_indirect, dib, BLOCK_SECTOR_SIZE, 0);
    for (int i = 0; i < 128 && allocated_sectors < sectors; i++)
      {
        if (dib->sectors[i] == BLOCK_SECTOR_NONE
            && !inode_indirect_allocate (&dib->sectors[i]))
          {
            cache_write (fs_device, *doubly_indirect, dib, BLOCK_SECTOR_SIZE,
                         0);
            free (dib);
            goto fail;
          }
        struct indirect_block *ib = malloc (sizeof (struct indirect_block));
        cache_read (fs_device, dib->sectors[i], ib, BLOCK_SECTOR_SIZE, 0);
        for (int j = 0; j < 128 && allocated_sectors < sectors; j++)
          {
            if (ib->sectors[j] != BLOCK_SECTOR_NONE)
              continue;

            block_sector_t *sector = &ib->sectors[j];
            if (free_map_allocate (1, sector))
              {
                ++allocated_sectors;
                cache_write (fs_device, *sector, zeros, BLOCK_SECTOR_SIZE, 0);
              }
            else
              {
                cache_write (fs_device, dib->sectors[i], ib, BLOCK_SECTOR_SIZE,
                             0);
                cache_write (fs_device, *doubly_indirect, dib,
                             BLOCK_SECTOR_SIZE, 0);
                free (ib);
                free (dib);
                goto fail;
              }
          }
        cache_write (fs_device, dib->sectors[i], ib, BLOCK_SECTOR_SIZE, 0);
        free (ib);
      }
    cache_write (fs_device, *doubly_indirect, dib, BLOCK_SECTOR_SIZE, 0);
    free (dib);
  }

  if (allocated_sectors >= sectors)
    return true;

  /* Free allocated resources if fail. */
fail:
  if (disk_inode->doubly_indirect != BLOCK_SECTOR_NONE)
    {
      struct indirect_block *dib = malloc (sizeof (struct indirect_block));
      cache_read (fs_device, disk_inode->doubly_indirect, dib,
                  BLOCK_SECTOR_SIZE, 0);
      for (int i = 127; i >= 0 && allocated_sectors > 0; --i)
        {
          if (dib->sectors[i] == BLOCK_SECTOR_NONE)
            continue;

          struct indirect_block *ib = malloc (sizeof (struct indirect_block));
          cache_read (fs_device, dib->sectors[i], ib, BLOCK_SECTOR_SIZE, 0);
          for (int j = 127; j >= 0 && allocated_sectors > 0; --j)
            if (ib->sectors[j] != BLOCK_SECTOR_NONE)
              {
                cache_free (fs_device, ib->sectors[j]);
                free_map_release (ib->sectors[j], 1);
                ib->sectors[j] = BLOCK_SECTOR_NONE;
                --allocated_sectors;
                if (allocated_sectors <= 0)
                  {
                    if (j == 0)
                      {
                        cache_free (fs_device, dib->sectors[i]);
                        free_map_release (dib->sectors[i], 1);
                        dib->sectors[i] = BLOCK_SECTOR_NONE;
                        if (i == 0)
                          {
                            cache_free (fs_device,
                                        disk_inode->doubly_indirect);
                            free_map_release (disk_inode->doubly_indirect, 1);
                            disk_inode->doubly_indirect = BLOCK_SECTOR_NONE;
                          }
                        else
                          cache_write (fs_device, disk_inode->doubly_indirect,
                                       dib, BLOCK_SECTOR_SIZE, 0);
                      }
                    else
                      {
                        cache_write (fs_device, dib->sectors[i], ib,
                                     BLOCK_SECTOR_SIZE, 0);
                        cache_write (fs_device, disk_inode->doubly_indirect,
                                     dib, BLOCK_SECTOR_SIZE, 0);
                      }
                    free (ib);
                    free (dib);
                    return false;
                  }
              }
          cache_free (fs_device, dib->sectors[i]);
          free_map_release (dib->sectors[i], 1);
          dib->sectors[i] = BLOCK_SECTOR_NONE;
          free (ib);
        }
      cache_free (fs_device, disk_inode->doubly_indirect);
      free_map_release (disk_inode->doubly_indirect, 1);
      disk_inode->doubly_indirect = BLOCK_SECTOR_NONE;
      free (dib);
    }

  if (disk_inode->indirect != BLOCK_SECTOR_NONE)
    {
      struct indirect_block *ib = malloc (sizeof (struct indirect_block));
      cache_read (fs_device, disk_inode->indirect, ib, BLOCK_SECTOR_SIZE, 0);
      for (int i = 127; i >= 0 && allocated_sectors > 0; --i)
        if (ib->sectors[i] != BLOCK_SECTOR_NONE)
          {
            cache_free (fs_device, ib->sectors[i]);
            free_map_release (ib->sectors[i], 1);
            ib->sectors[i] = BLOCK_SECTOR_NONE;
            --allocated_sectors;
            if (allocated_sectors <= 0)
              {
                if (i == 0)
                  {
                    cache_free (fs_device, disk_inode->indirect);
                    free_map_release (disk_inode->indirect, 1);
                    disk_inode->indirect = BLOCK_SECTOR_NONE;
                  }
                else
                  cache_write (fs_device, disk_inode->indirect, ib,
                               BLOCK_SECTOR_SIZE, 0);
                free (ib);
                return false;
              }
          }
      cache_free (fs_device, disk_inode->indirect);
      free_map_release (disk_inode->indirect, 1);
      disk_inode->indirect = BLOCK_SECTOR_NONE;
      free (ib);
    }

  for (int i = 9; i >= 0 && allocated_sectors > 0; --i)
    if (disk_inode->direct[i] != BLOCK_SECTOR_NONE)
      {
        cache_free (fs_device, disk_inode->direct[i]);
        free_map_release (disk_inode->direct[i], 1);
        disk_inode->direct[i] = BLOCK_SECTOR_NONE;
        --allocated_sectors;
      }

  return false;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir,
              block_sector_t parent)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->is_dir = is_dir;
      disk_inode->file_cnt = 0;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->parent = parent;
      for (int i = 0; i < 10; i++)
        disk_inode->direct[i] = BLOCK_SECTOR_NONE;
      disk_inode->indirect = BLOCK_SECTOR_NONE;
      disk_inode->doubly_indirect = BLOCK_SECTOR_NONE;
      if (inode_grow_unlocked (disk_inode, sectors))
        {
          cache_write (fs_device, sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire (&open_inodes_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release (&open_inodes_lock);
          return inode;
        }
    }

  /* Allocate. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  cache_read (fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  rwlock_init (&inode->rwlock);
  lock_release (&open_inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode == NULL)
    return NULL;
  lock_acquire (&inode_reopen_lock);
  rwlock_acquire_writer (&inode->rwlock);
  inode->open_cnt++;
  rwlock_release (&inode->rwlock);
  lock_release (&inode_reopen_lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (struct inode *inode)
{
  rwlock_acquire_reader (&inode->rwlock);
  block_sector_t inumber = inode->sector;
  rwlock_release (&inode->rwlock);
  return inumber;
}

/* Returns the block sector number of the parent directory of INODE. */
block_sector_t
inode_get_parent (struct inode *inode)
{
  rwlock_acquire_reader (&inode->rwlock);
  block_sector_t parent = inode->data.parent;
  rwlock_release (&inode->rwlock);
  return parent;
}

static void
inode_indirect_close (block_sector_t sector)
{
  struct indirect_block *ib = malloc (sizeof (struct indirect_block));
  cache_read (fs_device, sector, ib, BLOCK_SECTOR_SIZE, 0);

  for (int i = 0; i < 128; i++)
    if (ib->sectors[i] != BLOCK_SECTOR_NONE)
      {
        cache_free (fs_device, ib->sectors[i]);
        free_map_release (ib->sectors[i], 1);
      }
  cache_free (fs_device, sector);
  free_map_release (sector, 1);

  free (ib);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire (&open_inodes_lock);
  rwlock_acquire_writer (&inode->rwlock);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Write all cache block back to disk.
         Actually, we only need to write cache block belonging to this inode,
         but we flush all cache blocks as it is simpler and the performance is
         better with relatively small cache. */
      cache_flush (false);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          for (int i = 0; i < 10; i++)
            if (inode->data.direct[i] != BLOCK_SECTOR_NONE)
              {
                cache_free (fs_device, inode->data.direct[i]);
                free_map_release (inode->data.direct[i], 1);
              }

          if (inode->data.indirect != BLOCK_SECTOR_NONE)
            inode_indirect_close (inode->data.indirect);

          if (inode->data.doubly_indirect != BLOCK_SECTOR_NONE)
            {
              struct indirect_block *ib
                  = malloc (sizeof (struct indirect_block));
              cache_read (fs_device, inode->data.doubly_indirect, ib,
                          BLOCK_SECTOR_SIZE, 0);
              for (int i = 0; i < 128; i++)
                if (ib->sectors[i] != BLOCK_SECTOR_NONE)
                  inode_indirect_close (ib->sectors[i]);
              cache_free (fs_device, inode->data.doubly_indirect);
              free_map_release (inode->data.doubly_indirect, 1);
              free (ib);
            }

          cache_free (fs_device, inode->sector);
          free_map_release (inode->sector, 1);
        }

      rwlock_release (&inode->rwlock);
      lock_release (&open_inodes_lock);
      free (inode);
      return;
    }
  rwlock_release (&inode->rwlock);
  lock_release (&open_inodes_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  rwlock_acquire_writer (&inode->rwlock);
  inode->removed = true;
  rwlock_release (&inode->rwlock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  rwlock_acquire_reader (&inode->rwlock);

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector_unlocked (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* If the sector is invalid, sectors after current offset must be
         invalid. */
      if (sector_idx == BLOCK_SECTOR_NONE)
        break;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length_unlocked (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read sector from cache. */
      cache_read (fs_device, sector_idx, buffer + bytes_read, chunk_size,
                  sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  rwlock_release (&inode->rwlock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* When writing to a file does not extend the file, multiple processes should
     also be able to write a single file at once. */

  if (offset + size > inode->data.length)
    rwlock_acquire_writer (&inode->rwlock);
  else
    rwlock_acquire_reader (&inode->rwlock);

  /* Grow the file size if necessary. */
  if (offset + size > inode->data.length)
    {
      struct inode_disk *data = &inode->data;
      size_t sectors
          = bytes_to_sectors (offset + size) - bytes_to_sectors (data->length);
      if (!inode_grow_unlocked (data, sectors))
        {
          rwlock_release (&inode->rwlock);
          return 0; /* Allocation failed. */
        }
      data->length = offset + size;
      cache_write (fs_device, inode->sector, data, BLOCK_SECTOR_SIZE, 0);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector_unlocked (inode, offset);
      ASSERT (sector_idx != BLOCK_SECTOR_NONE);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length_unlocked (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Writes data to cache. */
      cache_write (fs_device, sector_idx, buffer + bytes_written, chunk_size,
                   sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  rwlock_release (&inode->rwlock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  ASSERT (inode != NULL);
  rwlock_acquire_writer (&inode->rwlock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  rwlock_release (&inode->rwlock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode != NULL);
  rwlock_acquire_writer (&inode->rwlock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  rwlock_release (&inode->rwlock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  ASSERT (inode != NULL);
  rwlock_acquire_reader (&inode->rwlock);
  off_t length = inode_length_unlocked (inode);
  rwlock_release (&inode->rwlock);
  return length;
}

static off_t
inode_length_unlocked (struct inode *inode)
{
  return inode->data.length;
}

bool
inode_is_dir (struct inode *inode)
{
  rwlock_acquire_reader (&inode->rwlock);
  bool ret = inode->data.is_dir;
  rwlock_release (&inode->rwlock);
  return ret;
}

int
inode_file_cnt (struct inode *inode)
{
  rwlock_acquire_reader (&inode->rwlock);
  cache_read (fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  int ret = inode->data.file_cnt;
  rwlock_release (&inode->rwlock);
  return ret;
}

void
inode_update_file_cnt (struct inode *inode, int delta)
{
  rwlock_acquire_writer (&inode->rwlock);
  cache_read (fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  inode->data.file_cnt += delta;
  cache_write (fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);
  rwlock_release (&inode->rwlock);
}

int
inode_open_cnt (struct inode *inode)
{
  rwlock_acquire_reader (&inode->rwlock);
  int ret = inode->open_cnt;
  rwlock_release (&inode->rwlock);
  return ret;
}
