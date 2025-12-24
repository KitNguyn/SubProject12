#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i, n;

  if(argc < 2){
    printf(1, "Usage: io_yielder <n>\n");
    exit();
  }

  n = atoi(argv[1]);
  for(i = 0; i < n; i++){
    sleep(1);   // nhường CPU cho process khác
  }

  exit();
}
