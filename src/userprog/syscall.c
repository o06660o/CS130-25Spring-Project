#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <bitmap.h>
#include <lib/user/syscall.h>
#include <stdio.h>
#include <syscall-nr.h>

/* file descriptor table. */
static struct bitmap *fd_table; /* Tracking allocated (1) and free (0) fds. */
static struct file *fd_entry[OPEN_FILE_MAX]; /* Maps fd to struct file *. */
static tid_t fd_owner[OPEN_FILE_MAX];        /* thread tid owning each fd. */
static struct lock fd_table_lock; /* Mutex protection for fd table access. */

/* Read data from the user stack, ESP won't be modified. */
#define READ(esp, delta, type)                                                \
  (*(type *)read_data (esp, &delta, sizeof (type)))
static void *read_data (const void *esp, size_t *delta, size_t size);
static bool is_valid_str (const char *str, size_t maxlen);
static bool is_valid_buf (const void *ptr, size_t size);

/* System calls. */
static void syscall_handler (struct intr_frame *);
static void halt_ (void);
static void exit_ (int status);
static int wait_ (pid_t pid);
static pid_t exec_ (const char *cmd_line);
static bool create_ (const char *file, unsigned initial_size);
static bool remove_ (const char *file);
static int open_ (const char *file);
static int filesize_ (int fd);
static int read_ (int fd, void *buffer, unsigned size);
static int write_ (int fd, const void *buffer, unsigned size);
static void seek_ (int fd, unsigned position);
static unsigned tell_ (int fd);
static void close_ (int fd);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fd_table = bitmap_create (OPEN_FILE_MAX);
  bitmap_set_multiple (fd_table, 0, 2, true);
  lock_init (&fd_table_lock);
  for (int i = 0; i < OPEN_FILE_MAX; ++i)
    fd_owner[i] = TID_ERROR;
}

/* Reads data from the user stack. Only checks that the address is not
   kernel address. */
static void *
read_data (const void *esp, size_t *delta, size_t size)
{
  void *st = (void *)((uint8_t *)esp + *delta);
  void *ed = (void *)((uint8_t *)st + size);
  /* We also check the start pointer since st + size might cause overflow. */
  if (st >= PHYS_BASE || ed > PHYS_BASE) /* ed == PHYS_BASE is allowed. */
    exit_ (-1);
  *delta += size;
  return st;
}

/* Checks if the string is not too long. If it uses kernel memory or points to
   NULL, the process will be terminated immediately. */
static bool
is_valid_str (const char *str, size_t maxlen)
{
  if (str == NULL) /* XXX: should NULL be allowed? */
    exit_ (-1);
  for (size_t i = 0; i < maxlen; i++)
    {
      if (is_kernel_vaddr (str + i))
        exit_ (-1);
      if (str[i] == '\0')
        return true; /* Ok. */
    }
  return false; /* Too long if we reach here. */
}

/* Checks if the buffer only uses user momory. Won't terminate the process. */
static bool
is_valid_buf (const void *ptr, size_t size)
{
  if (ptr >= PHYS_BASE || (void *)((uint8_t *)ptr + size) > PHYS_BASE)
    return false;
  return true;
}

/* The syscall handler, which is called when the user program pushes data into
   the stack and invokes int $0x30. */
static void
syscall_handler (struct intr_frame *f)
{
  size_t delta = 0;
  int syscall_num = READ (f->esp, delta, int);
#ifdef DEBUG
  printf ("syscall: %d\n", syscall_num);
#endif
  switch (syscall_num)
    {
    case SYS_HALT: /* Halt the operating system. */
      {
        halt_ ();
        NOT_REACHED ();
      }
    case SYS_EXIT: /* Terminate this process. */
      {
        int status = READ (f->esp, delta, int);
        exit_ (status);
        NOT_REACHED ();
      }
    case SYS_WAIT: /* Wait for a child process. */
      {
        pid_t pid = READ (f->esp, delta, pid_t);
        f->eax = wait_ (pid);
        break;
      }
    case SYS_EXEC: /* Start another process. */
      {
        const char *cmd_line = READ (f->esp, delta, const char *);
        f->eax = exec_ (cmd_line);
        break;
      }
    case SYS_CREATE: /* Create a file. */
      {
        const char *file = READ (f->esp, delta, const char *);
        unsigned initial_size = READ (f->esp, delta, unsigned);
        f->eax = create_ (file, initial_size);
        break;
      }
    case SYS_REMOVE: /* Delete a file. */
      {
        const char *file = READ (f->esp, delta, const char *);
        f->eax = remove_ (file);
        break;
      }
    case SYS_OPEN: /* Open a file. */
      {
        const char *file = READ (f->esp, delta, const char *);
        f->eax = open_ (file);
        break;
      }
    case SYS_FILESIZE: /* Obtain a file's size. */
      {
        int fd = READ (f->esp, delta, int);
        f->eax = filesize_ (fd);
        break;
      }
    case SYS_READ: /* Read from a file. */
      {
        int fd = READ (f->esp, delta, int);
        void *buffer = READ (f->esp, delta, void *);
        unsigned size = READ (f->esp, delta, unsigned);
        f->eax = read_ (fd, buffer, size);
        break;
      }
    case SYS_WRITE: /* Write to a file. */
      {
        int fd = READ (f->esp, delta, int);
        const void *buffer = READ (f->esp, delta, const void *);
        unsigned size = READ (f->esp, delta, unsigned);
        f->eax = write_ (fd, buffer, size);
        break;
      }
    case SYS_SEEK: /* Change position in a file. */
      {
        int fd = READ (f->esp, delta, int);
        unsigned position = READ (f->esp, delta, unsigned);
        seek_ (fd, position);
        break;
      }
    case SYS_TELL: /* Report current position in a file. */
      {
        int fd = READ (f->esp, delta, int);
        f->eax = tell_ (fd);
        break;
      }
    case SYS_CLOSE: /* Close a file. */
      {
        int fd = READ (f->esp, delta, int);
        close_ (fd);
        break;
      }

    default: /* Unkown syscall. */
      exit_ (-1);
    }
}

/* The halt syscall. */
static void
halt_ (void)
{
  shutdown_power_off ();
}

/* The exit syscall. */
static void
exit_ (int status)
{
  process_exit (status);
}

/* The wait syscall. */
static int
wait_ (pid_t pid)
{
  return process_wait (pid);
}

/* The exec syscall. */
static pid_t
exec_ (const char *cmd_line)
{
  if (!is_valid_str (cmd_line, CMDLEN_MAX))
    exit_ (-1);
  return process_execute (cmd_line);
}

/* The create syscall. */
static bool
create_ (const char *file, unsigned initial_size)
{
  if (!is_valid_str (file, NAME_MAX + 1))
    return false; /* File name too long. */

  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);

  return success;
}

/* The remove syscall. */
static bool
remove_ (const char *file)
{
  if (!is_valid_str (file, NAME_MAX + 1))
    return false; /* File name too long. */
  bool success = filesys_remove (file);
  return success;
}

/* The open syscall. */
static int
open_ (const char *file)
{
  if (!is_valid_str (file, NAME_MAX + 1))
    return -1; /* File name too long. */

  lock_acquire (&filesys_lock);
  struct file *open_file = filesys_open (file);
  lock_release (&filesys_lock);
  if (open_file == NULL)
    return -1;

  lock_acquire (&fd_table_lock);
  int fd_alloc = bitmap_scan (fd_table, 2, 1, false);
  if (fd_alloc == BITMAP_ERROR)
    {
      lock_release (&fd_table_lock);
      lock_acquire (&filesys_lock);
      file_close (open_file);
      lock_release (&filesys_lock);
      return -1;
    }
  bitmap_set (fd_table, fd_alloc, true);
  fd_owner[fd_alloc] = thread_current ()->tid;
  fd_entry[fd_alloc] = open_file;
  lock_release (&fd_table_lock);
  return fd_alloc;
}

/* The filesize syscall. */
static int
filesize_ (int fd)
{
  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return -1;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  int size = file_length (open_file);
  lock_release (&filesys_lock);
  return size;
}

/* The read syscall. */
static int
read_ (int fd, void *buffer, unsigned size)
{
  if (!is_valid_buf (buffer, size))
    exit_ (-1);

  if (fd == STDIN_FILENO)
    {
      for (unsigned i = 0; i < size; ++i)
        {
          *(uint8_t *)buffer = input_getc ();
          buffer += sizeof (uint8_t);
        }
      return size;
    }
  else if (fd == STDOUT_FILENO)
    return -1;

  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return -1;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int ret = file_read (open_file, buffer, size);
  lock_release (&filesys_lock);
  return ret;
}

/* The write syscall. */
static int
write_ (int fd, const void *buffer, unsigned size)
{
  if (!is_valid_buf (buffer, size))
    exit_ (-1);

  if (fd == STDOUT_FILENO)
    {
      putbuf ((const char *)buffer, size);
      return size;
    }
  else if (fd == STDIN_FILENO)
    return -1;

  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return -1;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int ret = file_write (open_file, buffer, size);
  lock_release (&filesys_lock);
  return ret;
}

/* The seek syscall. */
static void
seek_ (int fd, unsigned position)
{
  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (open_file, position);
  lock_release (&filesys_lock);
}

/* The tell syscall. */
static unsigned
tell_ (int fd)
{
  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return -1;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int ret = file_tell (open_file);
  lock_release (&filesys_lock);
  return ret;
}

/* The close syscall. */
static void
close_ (int fd)
{
  if (fd < 0 || fd >= OPEN_FILE_MAX || fd_owner[fd] != thread_current ()->tid)
    return;
  struct file *open_file = fd_entry[fd];
  if (open_file == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_close (open_file);
  lock_release (&filesys_lock);

  lock_acquire (&fd_table_lock);
  bitmap_set (fd_table, fd, false);
  fd_owner[fd] = TID_ERROR;
  fd_entry[fd] = NULL;
  lock_release (&fd_table_lock);
}
