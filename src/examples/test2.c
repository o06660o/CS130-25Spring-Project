/* test2.c

   Test whether unmodified pages are swapped out.

   Create a static array of 3MB, only read it instead of writing to it.
   The swap cannot be full if we run pintos with a relatively small swap size.
*/

/*
cd ../../examples/ && \
make && \
cd ../vm/build/ && \
pintos --filesys-size=2 --swap-size=0.5 -p ../../examples/test2 -a test2 \
-- -q -f run test2
*/

#include <stdio.h>
#include <syscall.h>

#define SIZE (4 * 1024 * 1024)

static volatile char read_only[SIZE];

int
main (void)
{
  printf ("test2: init\n");

  for (size_t i = 0; i < SIZE; i++)
    if (read_only[i] != 0)
      {
        printf ("test2: read-only array was modified\n");
        return EXIT_FAILURE;
      }

  return EXIT_SUCCESS;
}
