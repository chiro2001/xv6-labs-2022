#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

void find(char *path, char *name) {
  char path_new[512], *p;
  int fd;
  int fd2;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (*de.name == '\0') continue;
    strcpy(path_new, path);
    p = path_new + strlen(path_new);
    *p++ = '/';
    strcpy(p, de.name);
    fd2 = open(path_new, 0);
    if (fstat(fd2, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", path_new);
      close(fd);
      return;
    }
    if (strcmp(name, de.name) == 0) printf("%s\n", path_new);
    switch (st.type) {
      case T_FILE:
        break;

      case T_DIR:
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) break;
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(path_new)) {
          printf("find: path too long\n");
          break;
        }
        find(path_new, name);
        break;
    }
    close(fd2);
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("usage: find name_to_find\n");
    exit(0);
  }
  find(".", argv[1]);
  exit(0);
}
