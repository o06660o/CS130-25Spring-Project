/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>

#define max(a, b) ((a) > (b) ? (a) : (b))

static bool condvar_less (const struct list_elem *a, const struct list_elem *b,
                          void *aux UNUSED);

#define MAX_DEPTH 8 /* The limit on depth of nested priority donation. */
static void donate_up (struct thread *, int depth);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0)
    {
      sema->value--;
      success = true;
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  struct thread *max_waiter = NULL;
  if (!list_empty (&sema->waiters))
    {
      max_waiter = thread_list_max (&sema->waiters);
      list_remove (&max_waiter->elem);
      thread_unblock (max_waiter);
    }
  sema->value++;
  if (!intr_context () && max_waiter != NULL
      && thread_less (thread_current (), max_waiter))
    thread_yield ();
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++)
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++)
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();
  cur->parent = lock->holder;
  donate_up (cur, 0);

  sema_down (&lock->semaphore);

  enum intr_level old_level = intr_disable ();
  lock->holder = thread_current ();
  list_push_back (&lock->holder->locks, &lock->elem);
  intr_set_level (old_level);
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();
  cur->parent = lock->holder;
  donate_up (cur, 0);

  success = sema_try_down (&lock->semaphore);
  if (success)
    {
      enum intr_level old_level = intr_disable ();
      lock->holder = thread_current ();
      list_push_back (&lock->holder->locks, &lock->elem);
      intr_set_level (old_level);
    }
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable ();

  /* Reset parent-child relationship. */
  for (struct list_elem *it = list_begin (&lock->semaphore.waiters);
       it != list_end (&lock->semaphore.waiters); it = list_next (it))
    {
      struct thread *t = list_entry (it, struct thread, elem);
      t->parent = NULL;
    }

  /* Find new donated priority. */
  lock->holder->extra_priority = 0;
  list_remove (&lock->elem);
  for (struct list_elem *it = list_begin (&lock->holder->locks);
       it != list_end (&lock->holder->locks); it = list_next (it))
    {
      struct lock *l = list_entry (it, struct lock, elem);
      if (list_empty (&l->semaphore.waiters))
        continue;
      struct thread *t = thread_list_max (&l->semaphore.waiters);
      int pri = max (t->extra_priority, t->priority);
      lock->holder->extra_priority = max (lock->holder->extra_priority, pri);
    }

  lock->holder = NULL;
  intr_set_level (old_level);

  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* Recursively donate priority to the thread that holds the lock. */
static void
donate_up (struct thread *t, int depth)
{
  if (depth > MAX_DEPTH || t == NULL || t->parent == NULL)
    return;
  int donated = max (t->extra_priority, t->priority);
  t->parent->extra_priority = max (t->parent->extra_priority, donated);
  donate_up (t->parent, depth + 1);
}

/* One semaphore in a list. */
struct semaphore_elem
{
  struct list_elem elem;      /* List element. */
  struct semaphore semaphore; /* This semaphore. */
};

/* Comparision function to wake threads in a condition variable */
static bool
condvar_less (const struct list_elem *a, const struct list_elem *b,
              void *aux UNUSED)
{
  ASSERT (a != NULL && b != NULL);
  struct semaphore_elem *sema_a = list_entry (a, struct semaphore_elem, elem);
  struct semaphore_elem *sema_b = list_entry (b, struct semaphore_elem, elem);
  struct thread *ta = thread_list_max (&sema_a->semaphore.waiters);
  struct thread *tb = thread_list_max (&sema_b->semaphore.waiters);
  return thread_less (ta, tb);
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);

  enum intr_level old_level = intr_disable ();
  list_push_back (&cond->waiters, &waiter.elem);
  intr_set_level (old_level);

  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters))
    {
      enum intr_level old_level = intr_disable ();
      struct list_elem *e = list_max (&cond->waiters, condvar_less, NULL);
      list_remove (e);
      intr_set_level (old_level);

      sema_up (&list_entry (e, struct semaphore_elem, elem)->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

struct rwlock_waiter
{
  struct list_elem elem; /* List element for the waiters list. */
  bool is_writer;        /* True if this waiter is a writer. */
  struct semaphore sema; /* Semaphore for the waiter. */
};

static void
waiter_init (struct rwlock_waiter *waiter, bool is_writer)
{
  ASSERT (waiter != NULL);
  sema_init (&waiter->sema, 0);
  waiter->is_writer = is_writer;
}

/* Initializes RWLOCK. A read-write lock allows multiple
   threads to read a shared resource simultaneously, but only one
   thread to write to it at a time. */
void
rwlock_init (struct rwlock *rwlock)
{
  ASSERT (rwlock != NULL);
  lock_init (&rwlock->lock);
  list_init (&rwlock->waiters);
  rwlock->active_readers = 0;
  rwlock->active_writers = 0;
}

/* Acquires a read lock on RWLOCK, allowing multiple threads to
   read simultaneously. If a write lock is held, this function
   will block until the write lock is released.

   This function may sleep, so it must not be called within an
   interrupt handler. */
void
rwlock_acquire_reader (struct rwlock *rwlock)
{
  ASSERT (rwlock != NULL);
  ASSERT (!intr_context ());
  lock_acquire (&rwlock->lock);

  /* No wait shortcut. */
  if (list_empty (&rwlock->waiters) && rwlock->active_writers == 0)
    {
      rwlock->active_readers++;
      lock_release (&rwlock->lock);
      return;
    }

  struct rwlock_waiter w;
  waiter_init (&w, false);
  list_push_back (&rwlock->waiters, &w.elem);
  lock_release (&rwlock->lock);

  sema_down (&w.sema);
}

/* Acquires a write lock on RWLOCK, allowing exclusive access to
   the resource. If any read locks are held, this function will
   block until all read locks are released.

   This function may sleep, so it must not be called within an
   interrupt handler. */
void
rwlock_acquire_writer (struct rwlock *rwlock)
{
  ASSERT (rwlock != NULL);
  ASSERT (!intr_context ());
  lock_acquire (&rwlock->lock);

  /* No wait shortcut. */
  if (list_empty (&rwlock->waiters) && rwlock->active_readers == 0
      && rwlock->active_writers == 0)
    {
      rwlock->active_writers++;
      lock_release (&rwlock->lock);
      return;
    }

  struct rwlock_waiter w;
  waiter_init (&w, true);
  list_push_back (&rwlock->waiters, &w.elem);
  lock_release (&rwlock->lock);

  sema_down (&w.sema);
}

/* Releases the read or write lock on RWLOCK. The lock must be
   held by the current thread, but we do not check this condition. */
void
rwlock_release (struct rwlock *rwlock)
{
  ASSERT (rwlock != NULL);
  lock_acquire (&rwlock->lock);
  if (rwlock->active_readers > 0)
    rwlock->active_readers--;
  else if (rwlock->active_writers > 0)
    rwlock->active_writers--;
  else
    PANIC ("rwlock_release called without an active lock");

  if (rwlock->active_readers == 0 && rwlock->active_writers == 0)
    {
      while (!list_empty (&rwlock->waiters))
        {
          struct rwlock_waiter *waiter = list_entry (
              list_pop_front (&rwlock->waiters), struct rwlock_waiter, elem);
          if (waiter->is_writer)
            {
              if (!(rwlock->active_writers == 0
                    && rwlock->active_readers == 0))
                {
                  list_push_front (&rwlock->waiters, &waiter->elem);
                  break;
                }
              rwlock->active_writers++;
              sema_up (&waiter->sema);
            }
          else
            {
              if (rwlock->active_writers > 0)
                {
                  list_push_front (&rwlock->waiters, &waiter->elem);
                  break;
                }
              rwlock->active_readers++;
              sema_up (&waiter->sema);
            }
        }
    }
  lock_release (&rwlock->lock);
}
