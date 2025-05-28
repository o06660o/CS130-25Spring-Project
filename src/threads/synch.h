#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore
{
  unsigned value;      /* Current value. */
  struct list waiters; /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock
{
  struct thread *holder;      /* Thread holding lock (for debugging). */
  struct semaphore semaphore; /* Binary semaphore controlling access. */
  struct list_elem elem;      /* List elements used by struct thread. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition
{
  struct list waiters; /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

enum rwlock_state
{
  RWLOCK_READER, /* Reader state. */
  RWLOCK_WRITER, /* Writer state. */
  RWLOCK_READY   /* Ready state, no readers or writers. */
};

/* A reader first read-write lock. */
struct rwlock
{
  enum rwlock_state state;  /* Current state of the lock. */
  struct lock lock;         /* Lock for the condition variable. */
  struct condition readers; /* Condition variable for readers. */
  struct condition writers; /* Condition variable for writers. */
  unsigned holder_count;    /* Number of readers holding the lock. */
  struct thread *holder;    /* The writer holding the lock, if any. */
};

void rwlock_init (struct rwlock *);
void rwlock_acquire_reader (struct rwlock *);
void rwlock_acquire_writer (struct rwlock *);
void rwlock_release (struct rwlock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
