#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  int pid1, pid2;

  printf(1, "Launching CPU-bound and IO-bound processes...\n");

  pid1 = fork();
  if(pid1 < 0){
    printf(1, "fork pid1 failed\n");
    exit();
  }
  if(pid1 == 0){
    // Child 1: CPU bound (tăng số vòng để sống đủ lâu cho Ctrl+P)
    char *argv1[] = {"cpu_loop", "2000000000", 0};
    exec("cpu_loop", argv1);
    printf(1, "exec cpu_loop failed\n");
    exit();
  }

  pid2 = fork();
  if(pid2 < 0){
    printf(1, "fork pid2 failed\n");
    // reap child 1 nếu cần
    wait();
    exit();
  }
  if(pid2 == 0){
    // Child 2: IO bound
    char *argv2[] = {"io_yielder", "200", 0};
    exec("io_yielder", argv2);
    printf(1, "exec io_yielder failed\n");
    exit();
  }

  // (Tuỳ chọn) cho bạn kịp Ctrl+P thấy 2 process đang chạy
  // sleep(20);

  // Wait đủ 2 child
  for(int i = 0; i < 2; i++)
    wait();

  printf(1, "Mixed launcher finished.\n");
  exit();
}
