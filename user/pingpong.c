#include <stdint.h>
#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {
  int ping[2] = {0, 0};
  int pong[2] = {0, 0};
  pipe(ping);
  pipe(pong);
#define PINGPONG_MAX_BUFFER_SIZE 64
  int pid = fork();
  if (pid != 0) {
    // parent: send ping, recv pong
    close(ping[0]);
    close(pong[1]);
    char *data = argc > 1 ? argv[1] : "ping";
    // printf("parent: send `%s'\n", data);
    uint32_t data_len = strlen(data) + 1;
    write(ping[1], &data_len, sizeof(data_len));
    write(ping[1], data, data_len);
    read(pong[0], &data_len, sizeof(data_len));
    if (data_len >= PINGPONG_MAX_BUFFER_SIZE) {
      printf("Error: too large data with %d bytes!\n", data_len);
      exit(1);
    }
    data = (char *) malloc(sizeof(char) * data_len);
    read(pong[0], data, data_len);
    printf("%d: received %s\n", getpid(), data);
    close(ping[1]);
    close(pong[0]);
    free(data);
    wait(&pid);
  } else {
    // child: recv ping; send pong
    close(ping[1]);
    close(pong[0]);
    uint32_t data_len = 0;
    read(ping[0], &data_len, sizeof(data_len));
    if (data_len >= PINGPONG_MAX_BUFFER_SIZE) {
      printf("Error: too large data with %d bytes!\n", data_len);
      exit(1);
    }
    char *data = (char *) malloc(sizeof(char) * data_len);
    read(ping[0], data, data_len);
    close(ping[0]);
    // printf(" child: recv `%s'\n", data);
    printf("%d: received %s\n", getpid(), data);
    free(data);
    int is_alloc = 0;
    if (argc > 2) {
      int argv_str_len = strlen(argv[2]);
      if (argv_str_len >= PINGPONG_MAX_BUFFER_SIZE) {
        printf("Error: too large data with %d bytes!\n", argv_str_len);
        exit(1);
      }
      data = (char *) malloc(sizeof(char) * (argv_str_len + 1));
      is_alloc = 1;
      strcpy(data, argv[2]);
    } else {
      data = "pong";
    }
    data_len = strlen(data) + 1;
    write(pong[1], &data_len, sizeof(data_len));
    write(pong[1], data, data_len);
    close(pong[1]);
    if (is_alloc) {
      free(data);
    }
    exit(0);
  }
  exit(0);
}
