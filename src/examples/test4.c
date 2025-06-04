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

char buf[2000];

int
main (void)
{
  create ("file", 2000);

  int fd = open ("file");
  if (fd <= 1)
    {
      printf ("test4: open failed\n");
      return EXIT_FAILURE;
    }

  memset (buf, 'e', sizeof (buf));
  write (fd, buf, 2000);
  memset (buf, 'a', sizeof (buf));

  printf ("test4: read after EOF\n");

  seek (fd, 1000);
  int bytes_read = read (fd, buf, sizeof (buf));
  if (bytes_read != 1000)
    {
      printf ("test4: read failed, expected 1000 bytes, got %d\n", bytes_read);
      close (fd);
      return EXIT_FAILURE;
    }

  for (int i = 0; i < 1000; i++)
    if (buf[i] != 'e')
      {
        printf ("test4: read data mismatch at %d\n", i);
        close (fd);
        return EXIT_FAILURE;
      }

  printf ("test4: seek beyond EOF\n");
  seek (fd, 3000);
  memset (buf, 'a', sizeof (buf));
  bytes_read = read (fd, buf, sizeof (buf));

  if (bytes_read != 0)
    {
      printf ("test4: read failed, expected 0 bytes, got %d\n", bytes_read);
      close (fd);
      return EXIT_FAILURE;
    }

  unsigned file_size = filesize (fd);
  if (file_size != 2000)
    {
      printf ("test4: file size mismatch, expected 2000, got %u\n", file_size);
      close (fd);
      return EXIT_FAILURE;
    }

  printf ("test4: success\n");

  close (fd);
  return EXIT_SUCCESS;
}