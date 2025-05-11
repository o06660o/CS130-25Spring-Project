/* test3.c

   Tests if the sharing is implemented.

   Create 5 child processes using the same executable, which needs about 1.5MB
   of read-only data.

cd ../../examples/ && \
make && \
cd ../vm/build/ && \
pintos -k -T 60 --qemu --filesys-size=2 --swap-size=0.1 -p \
../../examples/test3 -a test3 -p \
../../examples/test3-child -a test3-child -- -q -f run test3
*/

#include <stdio.h>
#include <syscall.h>

#define CHILD_CNT 5

int
main (void)
{
  pid_t children[CHILD_CNT];
  int i;

  printf ("test3: start\n");
  for (i = 0; i < CHILD_CNT; i++)
    if ((children[i] = exec ("test3-child")) == -1)
      printf ("test3: exec test3-child %d failed\n", i);
    else
      printf ("test3: exec test3-child %d\n", i);

  for (i = 0; i < CHILD_CNT; i++)
    if (wait (children[i]) == 0x42)
      printf ("test3: wait for child %d\n", i);
    else
      {
        printf ("test3: wait for child %d failed\n", i);
        return EXIT_FAILURE;
      }
  return EXIT_SUCCESS;
}
