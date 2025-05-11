/* Child process for test3.
   Creates a child process and busy waits for some time. */

#include <stdint.h>
#include <stdio.h>

#define BUSY 50000000 /* 5e8 */
#define SIZE 1572864  /* 1.5MB */
#define UNUSED __attribute__ ((unused))
#define READ_ONLY __attribute__ ((section (".text")))

READ_ONLY UNUSED static const volatile uint8_t data[SIZE];
/* UNUSED static volatile uint8_t data[SIZE]; */

int
main (void)
{
  for (volatile int i = 0; i < BUSY; i++)
    ;
  for (volatile int i = 0; i < SIZE; i++)
    if (data[i] != 0)
      {
        printf ("test3-child: const_data[%d] = %d\n", i, data[i]);
        return -1;
      }
  return 0x42;
}
