/* test4.c

   Tests reading and seeking after EOF.
   It creates a file, writes 2000 bytes to it, and then starts reading at
   offset 1000, reading 2000 bytes.
   Then, seeking beyond EOF at offset 3000 and reading 2000 bytes.
   Reading and seeking after EOF doesn't lead to file growth.

cd ../../examples/ && \
make && \
cd ../filesys/build/ && \
pintos -k -T 60 --qemu --filesys-size=4 --swap-size=4 -p \
../../examples/test4 -a test4 -- -q -f run test4

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define MAX_FILE_SIZE (8 * 1024 * 1024) /* 8 MB */

char buf[512];

int
main (void)
{
  printf ("test4: begin\n");

  create ("file", 2000);
  int fd = open ("file");
  for (size_t ofs = 0; ofs <= MAX_FILE_SIZE + 200; ofs += 2000)
    {
      seek (fd, ofs);
      int __attribute__ ((unused)) _ = read (fd, buf, sizeof (buf));
    }
  close (fd);
  printf ("test4: success\n");

  return EXIT_SUCCESS;
}
