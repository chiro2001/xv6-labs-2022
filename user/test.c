#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  char c;

  while(read(0, &c, 1) == 1) {
    write(1, &c, 1);
  }
  exit(0);
}
