/* test1.c

   Currently, this file tests the behavior when a parent process terminates
   before its child.

   I made some changes to recursor.c and creates 4 processes:
   3 -> 2 -> 1 -> 1
   hoping their termination order to be:
   0 -> 2 -> 1 -> 3
   however, under my implementation it is:
   2 -> 3 -> 0 -> 1

   One possible usage:

cd ../../examples/ && \
make && \
cd ../userprog/build/ && \
pintos --filesys-size=2 -p ../../examples/test1 -a test1 \
-- -q -f run "test1 qwq 3 0"
*/

#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int
main (int argc, char *argv[])
{
  char buffer[128];
  pid_t pid;
  int retval = 0;

  if (argc != 4)
    {
      printf ("usage: test1 qwq 3 0\n");
      exit (1);
    }

  /* Print args. */
  printf ("%s %s %s %s\n", argv[0], argv[1], argv[2], argv[3]);

  /* Execute child and wait for it to finish if requested. */
  if (atoi (argv[2]) != 0)
    {
      snprintf (buffer, sizeof buffer, "test1 %s %d %s", argv[1],
                atoi (argv[2]) - 1, argv[3]);
      pid = exec (buffer);

      if (*argv[2] == '3')
        for (volatile int i = 0; i < 1000000000; i++)
          asm volatile ("");
      if (*argv[2] == '2')
        for (volatile int i = 0; i < 50; i++)
          asm volatile ("");
      if (*argv[2] == '1')
        for (volatile int i = 0; i < 500; i++)
          asm volatile ("");

      if (atoi (argv[3]))
        retval = wait (pid);
    }

  /* Done. */
  printf ("%s %s: dying, retval=%d\n", argv[1], argv[2], retval);
  exit (retval);
}
