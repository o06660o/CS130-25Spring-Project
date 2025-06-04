/* test5.c

   Tests reading and writing extremely large files.

cd ../../examples/ && \
make && \
cd ../filesys/build/ && \
pintos -k -T 240 --qemu --filesys-size=15 --swap-size=15 -p \
../../examples/test5 -a test5 -- -q -f run test5

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define MAX_FILE_SIZE (8 * 1024 * 1024) /* 8 MB */

char data[MAX_FILE_SIZE];
char buf[MAX_FILE_SIZE];

int
main (void)
{
  printf ("test5: begin\n");
  memset (data, 'a', sizeof (data));

  printf ("test5: writing\n");
  create ("file1", 2000);
  int fd = open ("file1");
  int bytes_write = write (fd, data, sizeof (data));

  ASSERT (bytes_write == sizeof (data));

  printf ("test5: reading\n");
  seek (fd, 0);
  int bytes_read = read (fd, buf, sizeof (buf));

  ASSERT (bytes_read == sizeof (buf));

  printf ("test5: verify\n");
  for (size_t i = 0; i < sizeof (buf); i++)
    {
      if (buf[i] != 'a')
        {
          printf ("test5: file contents changed at offset %d\n", i);
          close (fd);
          return EXIT_FAILURE;
        }
    }

  close (fd);
  printf ("test5: success\n");

  return EXIT_SUCCESS;
}
