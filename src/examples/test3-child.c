/* Child process for test3.
   Creates a child process and busy waits for some time.
*/

#include <stdint.h>

#define BUSY 500000000          /* 5e8 */
#define SIZE (20 * 1024 * 1024) /* 20MB */
#define UNUSED __attribute__ ((unused))
#define READ_ONLY __attribute__ ((section (".text")))

READ_ONLY UNUSED static const volatile uint8_t data[SIZE];
/* UNUSED static volatile uint8_t data[SIZE]; */

int
main (void)
{
  /* Busy waits for a while. */
  for (volatile int i = 0; i < BUSY; i++)
    ;
  return 0x42;
}
