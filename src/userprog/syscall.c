#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <lib/user/syscall.h>
#include <stdio.h>
#include <syscall-nr.h>

/* Read data from the user stack, ESP won't be modified. */
#define READ(esp, delta, type)                                                \
  (*(type *)read_data (esp, &delta, sizeof (type)))
static void *read_data (const void *esp, size_t *delta, size_t size);
static bool is_valid_ptr (const void *ptr) UNUSED;
static bool is_valid_str (const char *str, size_t maxlen) UNUSED;

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
}

void
syscall_exit (int status)
{
  exit_ (status);
}

/* Reads data from the user stack. Only checks that the address is not a
   kernel address. */
static void *
read_data (const void *esp, size_t *delta, size_t size)
{
  if (is_kernel_vaddr (esp + size))
    exit_ (-1);
  void *ret = (void *)((uint8_t *)esp + *delta);
  *delta += size;
  return ret;
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
  struct thread *cur = thread_current ();
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

/* The wait syscall. */
static int
wait_ (pid_t pid UNUSED)
{
  return process_wait (pid);
}

/* The exec syscall. */
static pid_t
exec_ (const char *cmd_line UNUSED)
{
  // TODO
  return -1;
}

/* The create syscall. */
static bool
create_ (const char *file UNUSED, unsigned initial_size UNUSED)
{
  // TODO
  return false;
}

/* The remove syscall. */
static bool
remove_ (const char *file UNUSED)
{
  // TODO
  return false;
}

/* The open syscall. */
static int
open_ (const char *file UNUSED)
{
  // TODO
  return -1;
}

/* The filesize syscall. */
static int
filesize_ (int fd UNUSED)
{
  // TODO
  return -1;
}

/* The read syscall. */
static int
read_ (int fd UNUSED, void *buffer UNUSED, unsigned size UNUSED)
{
  // TODO
  return -1;
}

/* The write syscall. */
static int
write_ (int fd, const void *buffer, unsigned size)
{
  // TODO
  if (fd == STDOUT_FILENO)
    {
      putbuf ((const char *)buffer, size);
      return size;
    }
  return -1;
}

/* The seek syscall. */
static void
seek_ (int fd UNUSED, unsigned position UNUSED)
{
  // TODO
}

/* The tell syscall. */
static unsigned
tell_ (int fd UNUSED)
{
  // TODO
  return -1;
}

/* The close syscall. */
static void
close_ (int fd UNUSED)
{
  // TODO
}
