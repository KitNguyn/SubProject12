#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  ps(); // gọi syscall ps
  exit();
}

