#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <list.h>
#include <stdio.h>
#include <string.h>

/* A directory. */
struct dir
{
  struct inode *inode; /* Backing store. */
  off_t pos;           /* Current position. */
};

/* A single directory entry. */
struct dir_entry
{
  block_sector_t inode_sector; /* Sector number of header. */
  char name[NAME_MAX + 1];     /* Null terminated file name. */
  bool in_use;                 /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent)
{
  off_t length = entry_cnt * sizeof (struct dir_entry);
  return inode_create (sector, length, true, parent);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL && inode_is_dir (inode))
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return NULL on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Returns the current working directory. */
struct dir *
dir_open_cwd (void)
{
  return dir_open (inode_open (thread_current ()->cwd));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  return dir->inode;
}

/* Walks through the directory structure starting from DIR. Returns true if
   successful, DIR and NAME will be updated during the walk. */
bool
dir_walk (struct dir **dir, char *ptr)
{
  /* Do nothing for empty path. */
  if (ptr == NULL || *ptr == '\0')
    return true;

  for (char *save_ptr = NULL, *token = strtok_r (ptr, "/", &save_ptr);
       token != NULL; token = strtok_r (NULL, "/", &save_ptr))
    {
      if (strcmp (token, ".") == 0)
        continue; /* Skip current directory. */
      if (strcmp (token, "..") == 0)
        {
          block_sector_t parent = inode_get_parent (dir_get_inode (*dir));
          struct dir *parent_dir = dir_open (inode_open (parent));
          if (parent_dir == NULL)
            return false;
          dir_close (*dir);
          *dir = parent_dir;
          continue; /* Move to parent directory. */
        }

      struct inode *inode = NULL;
      if (!dir_lookup (*dir, token, &inode))
        return false;
      if (!inode_is_dir (inode))
        {
          inode_close (inode);
          return false; /* Not a directory. */
        }
      struct dir *new_dir = dir_open (inode);

      if (new_dir == NULL)
        return false; /* Failed to open directory. */
      dir_close (*dir);
      *dir = new_dir;
    }
  return true;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (struct dir *dir, const char *name, struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (strcmp (name, ".") == 0)
    {
      if (ep != NULL)
        {
          e.in_use = true;
          e.inode_sector = inode_get_inumber (dir_get_inode (dir));
          strlcpy (e.name, ".", sizeof e.name);
          *ep = e;
        }
      if (ofsp != NULL)
        *ofsp = 0;
      return true;
    }
  if (strcmp (name, "..") == 0)
    {
      if (ep != NULL)
        {
          e.in_use = true;
          e.inode_sector = inode_get_parent (dir_get_inode (dir));
          strlcpy (e.name, "..", sizeof e.name);
          *ep = e;
        }
      if (ofsp != NULL)
        *ofsp = 0;
      return true;
    }

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (struct dir *dir, const char *name, struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;
  if (strcmp (name, ".") == 0 || strcmp (name, "..") == 0)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  inode_update_file_cnt (dir_get_inode (dir), 1);

done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if (inode_is_dir (inode))
    {
      if (!dir_empty (dir_open (inode)))
        {
          inode_close (inode);
          return false;
        }
      block_sector_t sector = inode_get_inumber (inode);
      if (sector == ROOT_DIR_SECTOR || sector == thread_current ()->cwd)
        {
          inode_close (inode);
          return false; /* Cannot remove root or current directory. */
        }
    }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

  inode_update_file_cnt (dir_get_inode (dir), -1);

done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

/* Returns true if DIR is empty, false otherwise.
   An empty directory contains no entries except for "." and "..". */
bool
dir_empty (struct dir *dir)
{
  return inode_file_cnt (dir_get_inode (dir)) == 0;
}
