#include <stdint.h>
#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {
  int p[2] = {0, 0};
  pipe(p);
  if (fork() == 0) {
    // parent: recv
    close(p[1]);
    char data[32];
    uint32_t data_len = 0;
    read(p[0], &data_len, sizeof(data_len));
    read(p[0], data, data_len);
    close(p[0]);
    printf("parent: recv `%s'\n", data);
  } else {
    close(p[0]);
    // child: send
    char *data = argc > 1 ? argv[1] : "test data";
    printf(" child: send `%s'\n", data);
    uint32_t data_len = strlen(data) + 1;
    write(p[1], &data_len, sizeof(data_len));
    write(p[1], data, data_len);
    close(p[1]);
    exit(0);
  }
  exit(0);
}
