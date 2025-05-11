/* test3.c

   Tests if the sharing is implemented.

   Trying to create child processes using the same executable, which needs
   about 20MB of read-only data. If sharing and demand paging are implemented
   correctly, we are still able to complete the test. If only paging is
   implemented, we are able to create at leat one child, but will run out
   of memory soon.

cd ../../examples/ && \
make && \
cd ../vm/build/ && \
pintos -k -T 60 --qemu --filesys-size=2 --swap-size=0.1 -p \
../../examples/test3 -a test3 -p \
../../examples/test3-child -a test3-child -- -q -f run test3
*/

#include <stdio.h>
#include <syscall.h>

#define MAX_CHILD 20

int
main (void)
{
  for (int i = 0; i < MAX_CHILD; i++)
    if (exec ("test3-child") == -1)
      {
        printf ("test3: exec test3-child %d failed\n", i);
        break;
      }
  return EXIT_SUCCESS;
}
