#include "types.h"
#include "stat.h"
#include "user.h"

// CPU-bound loop for testing MLFQ.
// Usage:
//   cpu_loop            -> run forever (easy to observe with Ctrl+P)
//   cpu_loop <n>        -> run about n iterations then exit
//
// Notes:
// - Uses volatile to prevent compiler optimizing the loop away.

static void
spin_forever(void)
{
  volatile uint x = 0;
  for(;;){
    x = x * 1664525 + 1013904223; // some cheap arithmetic
  }
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    // No argument: run forever so you can always see it in procdump (Ctrl+P)
    spin_forever();
    exit(); // never reaches
  }

  int n = atoi(argv[1]);
  if(n <= 0){
    printf(1, "Usage: cpu_loop <n>\n");
    exit();
  }

  volatile uint x = 0;
  for(int i = 0; i < n; i++){
    // Make each iteration non-trivial
    x = x * 1664525 + 1013904223;
    x ^= (x >> 16);
  }

  // prevent unused warning / optimization
  if(x == 0xdeadbeef)
    printf(1, "x=%d\n", x);

  exit();
}
