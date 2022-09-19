## 实验 util

### 为 xv6 添加一个用户应用程序

用户应用程序列表是 `Makefile` 文件中的 `UPROGS`，由 `_` 开头的 targets 将会被编译为用户程序并复制到 `fs.img` 内，从而在 xv6 的目录结构中就可以看到并执行一个用户应用程序了。

#### 修改 `Makefile`

在 `Makefile` 的 `UPROGS` 变量中添加了如下内容：

```makefile
UPROGS=\
	...
	$U/_sleep\
	$U/_pingpong\
	$U/_primes\
	$U/_find\
	$U/_test\
	$U/_xargs\
```

#### 添加用户应用程序源代码

以一个简单的读取并返回键盘内容的程序 `test` 为例。

新建文件 `user/test.c`：

```c
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
```

文件引入了 xv6 的系统调用对应的头文件，声明了 `main` 函数，不断读取标准输入并写入到标准输出中。

```shell
➜ chiro@chiro-pc  ~/os/xv6-labs-2022 git:(util) ✗ make qemu
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ test
Input text
Input text
$ QEMU: Terminated
```

输入 `Ctrl+D` 以终止读入键盘。

### pingpong

本程序需要打开两个管道并新建一个进程，主进程向第一个管道写入 "ping"，子进程从第一个管道读取到 "ping" 之后在第二个管道写入 "pong"，主线程读取第二个管道的数据并输出。

这里实现的程序对上述需求进行了一些改变：

1. 可以在调用 `pingpong` 的时候通过调用参数传入第一个、第二个管道需要发送的字符串信息，不传入参数则默认为 "ping"、"pong"。
2. 先在管道写入数据长度，再读取对应数据长度的数据。
3. 数据在内存中的占用是动态声明的，如果超过最大长度则退出，以保护系统和用户程序的安全。

```c
// pingpong.c
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
    // 主进程
    close(ping[0]);
    close(pong[1]);
    char *data = argc > 1 ? argv[1] : "ping";
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
    // 子进程
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
```

#### primes

本程序需要找到小于某一值的所有的质数。主要思路是：

1. 主进程打开一个管道，并启动一个子进程
2. 管道中的数字保证从小到大传输
3. 主进程向管道写入所有与当前数列最小值不互质的数
4. 子进程从管道读取数字，并作为主进程回到第一步
5. 当读取到的数列与数列最小值都互质的时候表示找到所有质数



