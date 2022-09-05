#include <stdint.h>

#include "kernel/types.h"
#include "user.h"

// TODO: fix bug that n > 400 exit abnormally

#define R (p[0])
#define W (p[1])
#define DATA_TYPE uint32_t
int main(int argc, char* argv[]) {
  if (argc <= 1) {
    printf("usage: %s n\n\tget all prime numbers in range [2, n]\n",
           argc == 0 ? "prime" : argv[0]);
    exit(-1);
  }
  int n = atoi(argv[1]);
  if (n <= 1) {
    exit(0);
  }
  DATA_TYPE* data = (DATA_TYPE*)malloc((n - 1) * sizeof(DATA_TYPE));
  int data_len = n - 1;
  for (int i = 0; i < n - 1; i++) data[i] = i + 2;
  int p[2] = {0, 0};
  while (1) {
    int done = 1;
    for (int i = 1; i < data_len; i++) {
      if (data[i] % data[0] != 0) {
        done = 0;
        break;
      }
    }
    if (done || data_len == 1) {
      for (int i = 0; i < data_len; i++) {
        printf("%d\t", data[0]);
      }
      printf("\n");
      // printf("done! data_len=%d\n", data_len);
      break;
    }
    pipe(p);
    // printf("fork!\n");
    int pid = fork();
    if (pid != 0) {
      // parent: send data
      close(R);
      // printf("append: %d\n", data[0]);
      printf("%d\t", data[0]);
      for (int i = 1; i < data_len; i++) {
        if (data[i] % data[0] != 0) {
          // if (data[i] == 1)
          //   printf("W: %d\n", data[i]);
          if (write(W, &data[i], sizeof(DATA_TYPE)) <= 0) {
            printf("write failed!\n");
          }
        }
      }
      close(W);
      wait(&pid);
      break;
    } else {
      // child: recv data
      close(W);
      int read_n = 0;
      data_len = 0;
      do {
        read_n = read(R, &data[data_len], sizeof(DATA_TYPE));
        data_len += read_n != 0;
        // if (read_n != 0) printf("R %d: %d\n", read_n, data[data_len - 1]);
      } while (read_n != 0);
      if (data_len == 0) break;
      close(R);
    }
  }
  exit(0);
}
