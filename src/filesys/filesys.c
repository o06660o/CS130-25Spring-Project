#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  cache_init ();
  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  inode_read_ahead_done ();
  cache_flush (true);
  free_map_close ();
}

/* Split the given NAME into a path name and a file name.
   If successful, the caller should free the allocated
    path_name and file_name. */
static bool
path_split (const char *name, char **path_name, char **file_name, bool is_dir)
{
  if (name == NULL || path_name == NULL || file_name == NULL)
    return false;

  char *name_copy = malloc (sizeof (char) * (strlen (name) + 2));
  char *ptr = name_copy;

  /* Interpret multiple slashes as single. */
  bool prev_slash = false;
  for (const char *ptr2 = name; *ptr2 != '\0'; ptr2++)
    if (*ptr2 == '/')
      {
        if (!prev_slash)
          {
            *ptr++ = *ptr2;
            prev_slash = true;
          }
      }
    else
      {
        prev_slash = false;
        *ptr++ = *ptr2;
      }
  if (is_dir && prev_slash)
    ptr--; /* Remove trailing slash for directories. */
  *ptr-- = '\0';
  if (*name_copy == '\0')
    {
      free (name_copy); /* Empty name not allowed. */
      return false;
    }

  /* Split path name and file name. */
  while (ptr >= name_copy && *ptr != '/')
    ptr--;
  if (ptr >= name_copy)
    {
      if (*(ptr + 1) == '\0')
        {
          free (name_copy); /* Empty file name not allowed. */
          return false;
        }
      *ptr++ = '\0';
      const char *path_ptr = name_copy;
      size_t path_len = strlen (path_ptr);
      *path_name = malloc (sizeof (char) * path_len + 2);
      strlcpy (*path_name, path_ptr, path_len + 1);
      if (path_len == 0)
        {
          (*path_name)[0] = '/';
          (*path_name)[1] = '\0';
        }

      const char *file_ptr = ptr;
      size_t file_len = strlen (file_ptr);
      *file_name = malloc (sizeof (char) * file_len + 1);
      strlcpy (*file_name, file_ptr, file_len + 1);
    }
  else
    {
      *path_name = malloc (sizeof (char) * 2);
      (*path_name)[0] = '.';
      (*path_name)[1] = '\0';
      size_t file_len = strlen (name_copy);
      *file_name = malloc (sizeof (char) * (file_len + 1));
      strlcpy (*file_name, name_copy, file_len + 1);
    }
  free (name_copy);
  return true;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  char *path_name = NULL, *file_name = NULL;
  if (!path_split (name, &path_name, &file_name, is_dir))
    return false;

  struct dir *dir = name[0] == '/' ? dir_open_root () : dir_open_cwd ();
  if (dir == NULL)
    {
      free (path_name);
      free (file_name);
      return false; /* Failed to open the directory. */
    }
  if (!dir_walk (&dir, path_name))
    {
      dir_close (dir);
      free (path_name);
      free (file_name);
      return false; /* Failed to walk the path. */
    }

  block_sector_t inode_sector = 0;
  bool ok = free_map_allocate (1, &inode_sector);
  if (ok)
    {
      struct inode *inode = dir_get_inode (dir);
      block_sector_t sector = inode_get_inumber (inode);
      ok &= inode_create (inode_sector, initial_size, is_dir, sector);
    }
  ok = ok && dir_add (dir, file_name, inode_sector);
  if (!ok && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free (path_name);
  free (file_name);
  return ok;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (name == NULL || *name == '\0')
    return NULL;
  /* Corner case: root path will be rejected by path_split(). */
  bool is_root = true;
  for (const char *ptr = name; *ptr != '\0'; ptr++)
    if (*ptr != '/')
      {
        is_root = false;
        break;
      }
  if (is_root)
    return file_open (inode_open (ROOT_DIR_SECTOR));

  char *path_name = NULL, *file_name = NULL;
  if (!path_split (name, &path_name, &file_name, false))
    return NULL; /* Invalid name. */
  struct dir *dir = name[0] == '/' ? dir_open_root () : dir_open_cwd ();
  if (!dir_walk (&dir, path_name))
    {
      dir_close (dir);
      free (path_name);
      free (file_name);
      return NULL; /* Failed to walk the path. */
    }

  struct inode *inode = NULL;
  dir_lookup (dir, file_name, &inode);
  dir_close (dir);
  free (path_name);
  free (file_name);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  if (name == NULL || *name == '\0')
    return false;
  /* Actually, this cannot determine if the name is a file or a directory. */
  bool is_dir = name[strlen (name) - 1] == '/';
  char *path_name = NULL, *file_name = NULL;
  if (!path_split (name, &path_name, &file_name, is_dir))
    return false;
  struct dir *dir = name[0] == '/' ? dir_open_root () : dir_open_cwd ();
  if (!dir_walk (&dir, path_name) || !dir_remove (dir, file_name))
    {
      dir_close (dir);
      free (path_name);
      free (file_name);
      return false;
    }

  dir_close (dir);
  free (path_name);
  free (file_name);

  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Changes the current working directory to NAME. */
bool
filesys_chdir (const char *name)
{
  /* Corner case: root path will be rejected by path_split(). */
  bool is_root = true;
  for (const char *ptr = name; *ptr != '\0'; ptr++)
    if (*ptr != '/')
      {
        is_root = false;
        break;
      }
  if (is_root)
    {
      struct dir *root_dir = dir_open_root ();
      if (root_dir == NULL)
        return false;
      thread_current ()->cwd = ROOT_DIR_SECTOR;
      return true;
    }

  char *path_name = NULL, *file_name = NULL;
  if (!path_split (name, &path_name, &file_name, true))
    return false; /* Invalid name. */
  struct dir *dir = name[0] == '/' ? dir_open_root () : dir_open_cwd ();
  if (!(dir_walk (&dir, path_name) && dir_walk (&dir, file_name)))
    {
      dir_close (dir);
      free (path_name);
      free (file_name);
      return false; /* Failed to walk the path. */
    }
  thread_current ()->cwd = inode_get_inumber (dir_get_inode (dir));
  dir_close (dir);
  free (path_name);
  free (file_name);
  return true;
}

/* Reads the next file name from the directory represented by FILE.
   If successful, stores the name in NAME and returns true.
   Returns false if there are no more files or if an error occurs.
   Fails if FILE is not a directory. */
bool
filesys_readdir (struct file *file, char *name)
{
  struct dir *dir = dir_open_ (file);
  bool success = dir_readdir (dir, name);
  dir_close_ (dir, file);
  return success;
}
