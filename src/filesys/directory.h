#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include "devices/block.h"
#include "filesys/file.h"
#include <stdbool.h>
#include <stddef.h>

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 30

struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t, size_t entry_cnt, block_sector_t parent);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_open_cwd (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);
struct dir *dir_open_ (struct file *);
void dir_close_ (struct dir *dir, struct file *file);

/* Reading and writing. */
bool dir_walk (struct dir **, char *name);
bool dir_lookup (struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);
bool dir_empty (struct dir *dir);

#endif /* filesys/directory.h */
