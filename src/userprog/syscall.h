#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void syscall_exit (int status); /* Terminate the current user program. */

#endif /* userprog/syscall.h */
