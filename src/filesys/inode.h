#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include <stdbool.h>

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool is_dir, block_sector_t parent);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (struct inode *);
block_sector_t inode_get_parent (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (struct inode *);
bool inode_is_dir (struct inode *);
int inode_inumber (struct inode *);
int inode_file_cnt (struct inode *);
void inode_update_file_cnt (struct inode *, int delta);
int inode_open_cnt (struct inode *inode);
void inode_read_ahead_done (void);

#endif /* filesys/inode.h */
