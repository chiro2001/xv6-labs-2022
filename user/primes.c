#include <stdint.h>

#include "kernel/types.h"
#include "user.h"

#define R(p) (p[0])
#define W(p) (p[1])
#define DATA_TYPE uint32_t
// #define DATA_TYPE uint8_t

int read_data(int p, void *dst, int len) {
  int l = 0;
  int r;
  while ((r = read(p, dst + l, 1)) == 1) {
    l++;
    if (l == len) return 0;
  }
  printf("read fd %d error, ret %d\n", p, r);
  return -1;
}

int main(int argc, char *argv[]) {
  int n;
  if (argc <= 1) {
    // printf("usage: %s n\n\tget all prime numbers in range [2, n]\n",
    //        argc == 0 ? "prime" : argv[0]);
    // exit(-1);
    n = 36;
  } else {
    n = atoi(argv[1]);
  }
  if (n <= 1) {
    exit(0);
  }

  int from[2] = {0, 0};
  if (pipe(from) < 0) {
    printf("cannot open pipe!\n");
    exit(1);
  }
  if (fork() != 0) {
    close(R(from));
    for (int i = 0; i < n - 1; i++) {
      DATA_TYPE d = (DATA_TYPE)(i + 2);
      write(W(from), &d, sizeof(DATA_TYPE));
    }
    wait(0);
  } else {
    while (1) {
      close(W(from));
      printf("from[%d, %d]\n", from[0], from[1]);
      DATA_TYPE prime;
      if (read_data(R(from), &prime, sizeof(DATA_TYPE)) != 0) {
        close(R(from));
        break;
      }
      printf("[%d] prime %d\n", getpid(), prime);
      // printf("prime %d\n", prime);
      int to[2] = {0, 0};
      if (pipe(to) < 0) {
        printf("cannot open pipe!\n");
        exit(1);
      }
      printf("  to[%d, %d]\n", to[0], to[1]);
      if (fork() != 0) {
        // 主进程
        close(R(to));
        DATA_TYPE d;
        while (read_data(R(from), &d, sizeof(DATA_TYPE)) == 0) {
          if (d % prime == 0) continue;
          if (write(W(to), &d, sizeof(DATA_TYPE)) < 0) {
            printf("[%d] write pipe %d failed! data: %d\n", getpid(), W(to), d);
            exit(1);
          }
        }
        close(R(from));
        close(W(to));
        wait(0);
        break;
      } else {
        // 子进程
        close(W(to));
        close(R(from));
        memcpy(from, to, sizeof(to));
      }
    }
  }
  exit(0);
}
