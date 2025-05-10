/* Child process for test3.
   Creates a child process and waits for it to finish.
*/

int
main (void)
{
  int a = 0;
  for (int i = 0; i < 10000000; i++)
    ++a;

  return 0x42;
}