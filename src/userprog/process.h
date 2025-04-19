#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <hash.h>

#define CMDLEN_MAX 4096 /* Max length of command line. */
#define ARGV_MAX 64     /* Max number of arguments. */

/* Used to track the exit value after a thread exits. */
struct exit_data
{
  tid_t tid;                 /* The thread id of the thread. */
  int exit_code;             /* The exit code of the thread. */
  bool called_process_wait;  /* Whether process_wait() has been called. */
  struct thread *father;     /* The father thread of the thread. */
  struct hash_elem hashelem; /* The hash element stored in `hash_exit_data'. */
  struct list_elem listelem; /* The list element stored in `ch_exit_data'. */
  struct semaphore die_sema; /* Wait for the thread to die. */
};

struct exit_data *tid_to_exit_data (tid_t); /* Get exit data by thread id. */

void process_init (void);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (int status);
void process_activate (void);

#endif /* userprog/process.h */
