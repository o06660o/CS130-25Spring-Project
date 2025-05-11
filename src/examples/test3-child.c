/* Child process for test3.
   Creates a child process and waits for it to finish.
*/

#include <stdint.h>
#include <stdio.h>

#define BUSY 500000000 /* 5e8 */
#define SIZE 1572864   /* 1.5MB */

__attribute__ ((section (".text"))) static const volatile uint8_t data[SIZE];

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
