/* Child process for test3.
   Creates a child process and waits for it to finish.
*/

#include <stdint.h>
#include <stdio.h>

static const uint8_t const_data[65536] = {};

int
main (void)
{
  for (int _ = 0; _ < 10; ++_)
    for (int i = 0; i < 65536; ++i)
      if (const_data[i] != 0)
        {
          printf ("test3-child: const_data[%d] = %d\n", i, const_data[i]);
          return -1;
        }

  return 0x42;
}