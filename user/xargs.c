#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
  char buf[512];
  if (argc <= 1) {
    printf("usage: xargs program args...\n");
    exit(0);
  }
  while (gets(buf, sizeof(buf))) {
    int n = 1;
    char **args = (char **)malloc(sizeof(char *) * MAXARG);
    char *last_p = buf;
    int in_word = 0;
    for (int i = 2; i < argc; i++) {
      args[n] = (char *)malloc(sizeof(char) * (strlen(argv[i]) + 1));
      strcpy(args[n], argv[i]);
      n++;
    }
    char *p = buf;
    args[0] = (char *)malloc(sizeof(char) * 512);
    strcpy(args[0], argv[1]);
    if (*p == '\0') break;
    if (*p != '\n') {
      while (*p) {
        if (!in_word) {
          if ((p == buf || (p != buf && *(p - 1) == ' '))) {
            if (*p != ' ') {
              last_p = p;
              in_word = 1;
            }
          }
        } else {
          if (*p == ' ' || *p == '\0' || *p == '\n') {
            in_word = 0;
            args[n] = (char *)malloc(sizeof(char) * 512);
            memcpy(args[n], last_p, p - last_p);
            args[n][p - last_p] = '\0';
            n++;
          }
        }
        if (*p == '\0' || *p == '\n') break;
        p++;
      }
    }
    args[n] = 0;
    int pid = fork();
    if (pid) {
      wait(&pid);
      for (int i = 0; i < n; i++) free(args[i]);
      free(args);
    } else {
      exec(args[0], args);
      exit(0);
    }
  }
  exit(0);
}
