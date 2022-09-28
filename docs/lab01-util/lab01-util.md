## 实验 util

### 为 xv6 添加一个用户应用程序

用户应用程序列表是 `Makefile` 文件中的 `UPROGS`，由 `_` 开头的 targets 将会被编译为用户程序并复制到 `fs.img` 内，从而在 xv6 的目录结构中就可以看到并执行一个用户应用程序了。

#### 修改 `Makefile`

在 `Makefile` 的 `UPROGS` 变量中添加了如下内容：

```make
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

```sh
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

运行：

```sh
$ pingpong A B
4: received A
3: received B
$ pingpong
6: received ping
5: received pong
$ 
```

### primes

本程序需要找到小于某一值的所有的质数。主要思路是：

1. 主进程打开一个管道，并启动一个子进程
2. 管道中的数字保证从小到大传输
3. 主进程向管道写入所有与当前数列最小值不互质的数
4. 子进程从管道读取数字，并作为主进程回到第一步
5. 当从管道拿不到数字的时候表示当前进程结束运行

主要需要注意的点：

1. 每次从管道读取的单位是字节数量，一次调用 `read()` 不一定能够返回制定数量的字节数量，得看返回值具体是多少。当表示一个数的字节数大于1，就需要注意等待完全读取多个字节表示的一个数才继续处理。
2. 一个管道对应的缓冲区大小是有限制的，在启动上述算法过程的时候不能直接将目标数字全部写入管道，必须同时启动其读取进程。
3. 使用上述算法的话，需要使用许多操作系统资源，所以当要求的数字过大的时候会因一个进程同时最多打开文件数量 `NOFILE`、最大进程数量 `NPROC` 而限制，造成程序无法继续执行。

运行 `primes` 默认参数是 36。

运行：

```sh
$ primes
prime 2
prime 3
prime 5
prime 7
prime 11
prime 13
prime 17
prime 19
prime 23
prime 29
prime 31
[user/primes.c:59 main] [15] done
$ primes 100
prime 2
prime 3
prime 5
prime 7
prime 11
prime 13
prime 17
prime 19
prime 23
prime 29
prime 31
prime 37
prime 41
prime 43
prime 47
prime 53
prime 59
prime 61
prime 67
prime 71
prime 73
prime 79
prime 83
prime 89
prime 97
[user/primes.c:59 main] [56] done
$ primes 300
prime 2
prime 3
prime 5
prime 7
# ...
prime 257
prime 263
prime 269
prime 271
prime 277
prime 281
prime 283
$ primes 1000 
prime 2
prime 3
prime 5
prime 7
# ...
prime 197
prime 199
prime 211
prime 223
prime 227
[user/primes.c:65 main ERROR] [127] cannot open pipe!
$ 
```

### find

类似 Unix 中的 `find` 命令，需要输入开始搜索的文件夹以及需要搜索的名字，每行输出一个按名字找到的文件。

程序部分参照了 `ls.c`。

```c
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
```

运行：

```sh
$ find . ls
./ls
$ mkdir aaa
$ echo b >> aaa/bbb
$ find bbb
usage: find start_dir name
$ find . bbb
./aaa/bbb
$ 
```

### xargs

`xargs` 命令基本作用是将标准输入中传入的文本再次转为参数列表，添加到新命令行参数之后。

一些需要注意的细节：

1. `argv` 是一个指针数组，每个元素对应一个字符串参数。
2. `argv[argc-1]` 必须为 0。
3. 程序逻辑是首先启动新进程 `fork()`，然后子进程切换到程序 `exec()`，主进程等待子进程运行完成。

运行：

```sh
$ echo 3 4|xargs echo 1 2
1 2 3 4
$ 
```

以上细节代码请参照源码对应文件。

### 回答问题

#### 阅读 `sleep.c`，回答下列问题

1. 当用户在 xv6 的 shell 中，输入了命令`sleep hello world\n`，请问 `argc` 的值是多少，`argv` 数组大小是多少。

   `argc == 3`, `argv == {"sleep", "hello", "world"}`，所以 `argv` 大小为 3。

2. 请描述 `main` 函数参数 `argv` 中的指针指向了哪些字符串，他们的含义是什么。

   `argv[0] -> "sleep"`，`argv[0]` 应当指向调用了这个程序的 shell 所输入的程序名称，例如可以是 `sleep` 或是 `./sleep`、`/sleep` 等。

   `argv[1] -> "hello", argv[2] -> "world"`，`argv[1]` 及其之后的指针指向调用这个程序的 shell 所输入的参数。

3. 哪些代码调用了系统调用为程序 `sleep` 提供了服务？

   1. `printf` 调用了 `putc`，其调用了 `write` 这一系统调用。
   
   2. `sleep(int)` 函数也是一个系统调用。
   
   3. 程序的退出 `exit(int)` 函数是将当前进程结束的系统调用。
   
      所以 `sleep.c` 总共调用了 3 个系统调用。

#### 了解管道模型，回答下列问题

1. 简要说明你是怎么创建管道的，又是怎么使用管道传输数据的。

   1. 使用 `pipe(int*)` 系统调用创建管道。`pipe(int*)` 会创建一个管道，并将管道对应的两个文件描述符写入传入的地址的第一、第二个位置。
   2. 我们可以指定管道的第一个文件描述符是用于管道读，第二个文件描述符用于管道写，创建好管道之后使用 `fork()` 创建子进程，在主进程和子进程中分别关闭读或者写端，即可使用这个管道在子进程和主进程之间传递数据。
   3. 读取、写入管道可以使用 `read`、`write` 系统调用。
   4. 当管道缓冲区满或其他错误情况，`write` 将会返回负值；当管道内数据为空且写端未关闭，`read` 将会阻塞；当管道中数据为空且写端已经关闭，`read` 将会返回 0；当进程被 kill，`read` 返回负值。

2. fork之后，我们怎么用管道在父子进程传输数据？

   首先关闭当前进程不需要使用的一端，然后对写端使用 `write` 系统调用，对读端使用 `read` 系统调用。

3. 试解释，为什么要提前关闭管道中不使用的一端？（提示：结合管道的阻塞机制）


> 考虑以下程序：
>
> ```c
> int main(int argc, char *argv[]) {
>   int p[2] = {0, 0};
>   pipe(p);
>   if (fork() != 0) {
>     close(p[0]);
>     printf("send: %d %d\n", p[0], p[1]);
>     write(p[1], p, sizeof(p));
>     close(p[1]);
>     wait(0);
>   } else {
>     close(p[1]);
>     int d[2] = {0, 0};
>     read(p[0], d, sizeof(p));
>     printf("recv: %d %d\n", d[0], d[1]);
>     int ret = read(p[0], d, sizeof(p));
>     if (ret <= 0) {
>       printf("read return: %d\n", ret);
>       exit(0);
>     }
>     printf("recv: %d %d\n", d[0], d[1]);
>     close(p[0]);
>   }
>   exit(0);
> }
> ```
>
> 这个程序在主进程通过管道向子进程发送了两个 `int` 类型的数据，并且提前关闭了管道不使用的一端。程序输出如下：
>
> ```sh
> $ test
> send: 3 4
> recv: 3 4
> read return: 0
> $ 
> ```
>
> 在子进程的第二个 `read `因为管道的写端已经关闭，所以直接返回了 0，没有继续读取数据。删除关闭不用的管道端口的代码：
>
> ```c
> int main(int argc, char *argv[]) {
>   int p[2] = {0, 0};
>   pipe(p);
>   if (fork() != 0) {
>     // close(p[0]);
>     printf("send: %d %d\n", p[0], p[1]);
>     write(p[1], p, sizeof(p));
>     close(p[1]);
>     wait(0);
>   } else {
>     // close(p[1]);
>     int d[2] = {0, 0};
>     read(p[0], d, sizeof(p));
>     printf("recv: %d %d\n", d[0], d[1]);
>     int ret = read(p[0], d, sizeof(p));
>     if (ret <= 0) {
>       printf("read return: %d\n", ret);
>       exit(0);
>     }
>     printf("recv: %d %d\n", d[0], d[1]);
>     close(p[0]);
>   }
>   exit(0);
> }
> ```
>
> 再次运行：
>
> ```sh
> $ test
> send: 3 4
> recv: 3 4
> ```
>
> 可以看到子进程因为第二个 `read`，阻塞在读取数据的时候，从而造成整个程序卡死。
>
> 查看管道相关代码：
>
> ```c
> int
> pipealloc(struct file **f0, struct file **f1)
> {
>   struct pipe *pi;
> 
>   pi = 0;
>   *f0 = *f1 = 0;
>   if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
>     goto bad;
>   if((pi = (struct pipe*)kalloc()) == 0)
>     goto bad;
>   pi->readopen = 1;
>   pi->writeopen = 1;
>   pi->nwrite = 0;
>   pi->nread = 0;
>   initlock(&pi->lock, "pipe");
>     // 生成的两个文件结构体，第一个只读，第二个只写
>   (*f0)->type = FD_PIPE;
>   (*f0)->readable = 1;
>   (*f0)->writable = 0;
>   (*f0)->pipe = pi;
>   (*f1)->type = FD_PIPE;
>   (*f1)->readable = 0;
>   (*f1)->writable = 1;
>   (*f1)->pipe = pi;
>   return 0;
> 
>  bad:
>   if(pi)
>     kfree((char*)pi);
>   if(*f0)
>     fileclose(*f0);
>   if(*f1)
>     fileclose(*f1);
>   return -1;
> }
> 
> // Close file f.  (Decrement ref count, close when reaches 0.)
> void
> fileclose(struct file *f)
> {
>   struct file ff;
> 
>   acquire(&ftable.lock);
>   if(f->ref < 1)
>     panic("fileclose");
>   // 当文件打开记数大于1的时候，不进行实际上的操作
>   if(--f->ref > 0){
>     release(&ftable.lock);
>     return;
>   }
>   ff = *f;
>   f->ref = 0;
>   f->type = FD_NONE;
>   release(&ftable.lock);
> 
>   if(ff.type == FD_PIPE){
>     pipeclose(ff.pipe, ff.writable);
>   } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
>     begin_op();
>     iput(ff.ip);
>     end_op();
>   }
> }
> 
> // 由 fileclose() 调用
> void
> pipeclose(struct pipe *pi, int writable)
> {
>   acquire(&pi->lock);
>     // 由文件结构体的 writeable 决定关闭写端还是读端
>   if(writable){
>     pi->writeopen = 0;
>     wakeup(&pi->nread);
>   } else {
>     pi->readopen = 0;
>     wakeup(&pi->nwrite);
>   }
>   if(pi->readopen == 0 && pi->writeopen == 0){
>     release(&pi->lock);
>     kfree((char*)pi);
>   } else
>     release(&pi->lock);
> }
> 
> int
> piperead(struct pipe *pi, uint64 addr, int n)
> {
>   int i;
>   struct proc *pr = myproc();
>   char ch;
> 
>   acquire(&pi->lock);
>     // 如果管道一直可写，阻塞在这个地方
>   while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
>     if(pr->killed){
>       release(&pi->lock);
>       return -1;
>     }
>     sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
>   }
>   for(i = 0; i < n; i++){  //DOC: piperead-copy
>     if(pi->nread == pi->nwrite)
>       break;
>     ch = pi->data[pi->nread++ % PIPESIZE];
>     if(copyout(pr->pagetable, addr + i, &ch, 1) == -1)
>       break;
>   }
>   wakeup(&pi->nwrite);  //DOC: piperead-wakeup
>   release(&pi->lock);
>   return i;
> }
> ```
>
> xv6 的管道是第一个 `fd` 为读，第二个 `fd` 为写。程序在 `fork()` 之后，从主进程复制出子进程，两个进程的 `PCB->ofile[]` 内同时持有对同一个管道的文件引用，此管道的文件的 `ref` 引用由此变成了 2。如提前调用 `close` 来关闭用不到的管道端口，将会降低两个进程中这个文件的 `ref` 到 1，使得传输完成后调用的 `close` 能够实在地关闭管道，设置管道的 `writeopen=0`。如果没有关闭用不到的管道端口，文件的 `ref` 就会保持在 2，使得传输完成时调用的 `close` 只将文件的引用记数 `ref` 减 1 而不会关闭管道，使得读管道操作继续阻塞。





