#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "threads/synch.h"
#include <debug.h>
#include <fixed.h>
#include <heap.h>
#include <list.h>
#include <stdint.h>
#ifdef VM
#include <lib/user/syscall.h>
#endif
#ifdef FILESYS
#include "devices/block.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

#ifdef USERPROG
/* States when a process loads the executable. */
enum load_status
{
  LOAD_READY,   /* Not loading but ready to load. */
  LOAD_SUCCESS, /* Loading the executable. */
  LOAD_FAIL     /* Loading fails. */
};
#endif

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* Thread nice values. */
#define NICE_MIN -20   /* Lowest nice value. */
#define NICE_DEFAULT 0 /* Default nice value. */
#define NICE_MAX 20    /* Highest nice value. */

/* Thread recent_cpu. */
#define RECENT_CPU_DEFAULT INT_TO_FP (0) /* Default recent_cpu. */

/* System load average. */
#define LOAD_AVG_DEFAULT INT_TO_FP (0) /* Default load average. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  int nice;                  /* Nice value. */
  fp32_t recent_cpu;         /* Recent CPU. */
  struct list_elem allelem;  /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */
  int extra_priority;    /* Extra priority by priority donation. */
  struct thread *parent; /* The thread that holds a lock waiting for. */
  struct list locks;     /* Locks this thread own. */

  /* Owned by devices/timer.c */
  int64_t wakeup_tick;       /* Time when the thread stops sleeping. */
  struct heap_elem heapelem; /* Heap element for sleeping queue. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */

  struct thread *creator;          /* The thread that creates this thread. */
  enum load_status ch_load_status; /* Load status. */
  struct semaphore ch_load_sema;   /* Semaphore for loading. */

  struct list ch_exit_data; /* List of child threads. */

  struct file *exec_file; /* Loaded executable file. */
#endif
#ifdef VM
  struct list page_list; /* Pages a user process owns. */
  void *user_esp; /* Stores esp on the transition from user to kernel mode */
  mapid_t mapid_next; /* Next mapid for this process. */
#endif
#ifdef FILESYS
  block_sector_t cwd; /* Current working directory. */
#endif

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

struct thread *tid_to_thread (tid_t); /* Find thread by its tid. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
void thread_calc_recent_cpu (struct thread *t, void *aux UNUSED);
void thread_calc_load_avg (void);

bool thread_less (const struct thread *, const struct thread *);
bool thread_list_less (const struct list_elem *, const struct list_elem *,
                       void *aux UNUSED);
struct thread *thread_list_max (struct list *);

#endif /* threads/thread.h */
