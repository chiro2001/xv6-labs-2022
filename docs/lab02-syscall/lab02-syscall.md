## 实验 syscall

### 在系统中添加系统调用：trace

实验任务要求为添加一个 `trace` 系统调用。

1. `user.h` 中添加这个系统调用的函数声明：`int trace(int);`

2. `syscall.h` 中添加系统调用号：`#define SYS_trace  22`

3. `usys.pl` 中添加一个 entry：`entry("trace");`

4. `syscall.c` 中添加声明等：

   ```c
   extern uint64 sys_trace(void);
   // ...
   static uint64 (*syscalls[])(void) = {
       // ...
       [SYS_trace] sys_trace, [SYS_sysinfo] sys_sysinfo, [SYS_checkvm] sys_checkvm,
   };
   const char syscall_names[][10] = {
       //...
       "trace",
   };
   ```

4. `sysproc.c` 中添加 `trace` 逻辑

   ```c
   // asserts: pid never overflows
   int sys_trace_child_pids[1024];
   int sys_trace_child_pids_tail = 0;
   
   uint64 sys_trace(void) {
     if (sys_trace_info.p) return -1;
     sys_trace_info.p = myproc();
     argint(0, &sys_trace_info.mask);
     // printf("pid %d tracing %d...\n", sys_trace_info.p->pid, sys_trace_info.mask);
     if (SYS_trace & sys_trace_info.mask)
       printf("%d: sys_%s(%d) -> %d\n", sys_trace_info.p->pid, syscall_names[SYS_trace], sys_trace_info.p->trapframe->a0, 0);
     sys_trace_child_pids[0] = sys_trace_info.p->pid;
     sys_trace_child_pids_tail = 1;
     return 0;
   }
   ```

5. `syscall.c` 中修改 `syscall()` 的逻辑

   ```c
   void syscall(void) {
     int num;
     struct proc *p = myproc();
   
     int will_trace = 0;
     if (sys_trace_info.p) {
       int tail = 0;
       while (tail != sys_trace_child_pids_tail) {
         if (sys_trace_child_pids[tail] == sys_trace_info.p->pid) {
           will_trace = 1;
           break;
         }
         tail++;
       }
     }
   
     num = p->trapframe->a7;
     if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
       int a = p->trapframe->a0;
       p->trapframe->a0 = syscalls[num]();
       if (will_trace && ((1 << num) & sys_trace_info.mask)) {
         printf("%d: sys_%s(%d) -> %d\n", p->pid, syscall_names[num], a, p->trapframe->a0);
       }
     } else {
       printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
       p->trapframe->a0 = -1;
     }
   }
   ```

6. `defs.h` 中添加一些用到的全局声明：

   ```c
   // sys trace
   struct sys_trace_info {
     struct proc* p;
     int mask;
   };
   extern struct sys_trace_info sys_trace_info;
   extern int sys_trace_child_pids[];
   extern int sys_trace_child_pids_tail;
   ```

添加的 `trace`、`sysinfo` 两个 Syscall 请在代码中具体查看。

### 添加系统调用：sysinfo

1. 需要仔细思考并掌握 `copyin/copyout` 的用法。
2. 需要检查目标地址是否正确、是否可写

```c
uint64 sys_sysinfo(void) {
  struct proc *p = myproc();
  struct sysinfo *d;
  struct sysinfo st;
  if (argaddr(0, (uint64*)&d) < 0) return -1;
  // check bad address
  int is_bad_addr = 0;
  uint64 tmp;
  for (uint8 i = 0; i < sizeof(struct sysinfo) / sizeof(uint64); i++) {
    uint64 addr = (uint64)d + sizeof(uint64) * i;
    if (fetchaddr(addr, &tmp) < 0) {
      is_bad_addr = 1;
      break;
    }
  }
  if (is_bad_addr) return -1;
  st.freemem = kpagefree() * PGSIZE;
  st.nproc = procn();
  st.freefd = fdfree();
  Dbg("sysinfo { freemem=%x, nproc=%d, freefd=%d }", st.freemem, st.nproc, st.freefd);
  copyout(p->pagetable, (uint64)d, (char*)&st, sizeof(st));
  return 0;
}
```

收集的信息：

1. `kpagefree()`：还剩下多少内存页未分配

   1. `kalloc.c` 中的内存管理是类似链表的内存管理
   2. 启动时将可分配的页以链表形式储存起来，需要申请内存时候取链表头即可
   3. 遍历一遍链表即可得到还剩多少内存页未分配，乘上页面大小即为剩余内存大小

2. `procn()`：正在运行的进程有多少个

   遍历 `proc[]`，`p->state == UNUSED` 的即为运行进程。

3. `fdfree()`：对当前进程还剩下多少文件描述符可以用

   返回 `p->ofile[fd] == 0` 的数量。

### 回答问题

1. 阅读`kernel/syscall.c`，试解释函数 `syscall()` 如何根据系统调用号调用对应的系统调用处理函数（例如`sys_fork`）？`syscall()` 将具体系统调用的返回值存放在哪里？

   > 当用户程序调用一个系统调用，例如 `fork()`，从用户程序到系统调用的具体函数 `sys_fork()` 的过程是这样的：
   >
   > 1. 添加在 `usys.pl` 脚本中的 `entry("fork")` 由 Perl 生成了 `usys.S`，包含将系统调用号写入 `a7` 寄存器并触发系统调用的程序。
   >
   > 2. 用户程序调用 `fork()`，`fork()` 对应的汇编代码为：
   >
   >    ```
   >    .global fork
   >    fork:
   >     li a7, SYS_fork
   >     ecall
   >     ret
   >    ```
   >
   >    将会把系统调用号 `SYS_fork` 写入 `a7` 寄存器并调用 `ecall` 以从用户模式 `U-Mode` 进入机器模式 `M-Mode`，跳转到 `trampoline` 内的 `uservec`。函数 `uservec` 用于将用户程序的 CPU 状态保存到 `trapframe` 中，方便内核处理，并跳转到 `usertrap()` 函数。
   >
   >    跳转到 xv6 对应中断、系统调用异常的处理函数 `void usertrap(void)`。
   >
   > 3. 在 `usertrap()` 内，判断中断/异常来源是系统调用，则调用 `syscall()` 函数。
   >
   > 4. `syscall()` 内，调用 `syscalls[p->trapframe->a7]()` 函数。 `syscalls[]` 是根据系统中断号排列的函数指针数组，对应于各个系统中断的操作逻辑的具体函数。
   >
   > 5. 于是经过函数指针 `syscalls[SYS_fork]` 调用到了函数 `sys_fork()` 函数。
   >
   > 调用系统中断之后的返回值会填入 `p->trapframe->a0`，`a0` 寄存器对应的是 Application Binary Interface 中的函数返回值。`p->trapframe` 将会在之后恢复到 CPU 中，CPU 回到用户态继续执行用户程序。

2. 阅读`kernel/syscall.c`，哪些函数用于传递系统调用参数？试解释 `argraw()` 函数的含义。

   > `syscall.c` 中用于传递系统调用参数的函数：
   >
   > 1. `int fetchaddr(uint64 addr, uint64 *ip)`：从用户地址空间中取 2-words 数据
   > 2. `int fetchstr(uint64 addr, char *buf, int max)`：用户地址空间取一个字符串
   > 3. `static uint64 argraw(int n)`：取第 `n` 个参数
   > 4. `int argint(int n, int *ip)`：取第 `n` 个 `int` 型参数
   > 5. `int argaddr(int n, uint64 *ip)`：取第 `n` 个地址型参数
   > 6. `int argstr(int n, char *buf, int max)`：取第 `n` 个字符串型参数
   >
   > `argraw()` 的含义：
   >
   > ```c
   > static uint64 argraw(int n) {
   >   struct proc *p = myproc();
   >   switch (n) {
   >     case 0:
   >       return p->trapframe->a0;
   >     case 1:
   >       return p->trapframe->a1;
   >     case 2:
   >       return p->trapframe->a2;
   >     case 3:
   >       return p->trapframe->a3;
   >     case 4:
   >       return p->trapframe->a4;
   >     case 5:
   >       return p->trapframe->a5;
   >   }
   >   panic("argraw");
   >   return -1;
   > }
   > ```
   >
   > 从 PCB 中取出当前系统中断发生时，用户程序的 CPU 状态 `p->trapframe`，由于 ABI 中规定 `a0` ~ `a5`  对应函数调用的第 1~5 个参数，所以将 `trapfram` 内的 `a[n]` 取出并返回就得到了函数调用时传入的第 `n` 个参数。

3. 阅读`kernel/proc.c`和`proc.h`，进程控制块存储在哪个数组中？进程控制块中哪个成员指示了进程的状态？一共有哪些状态？

   > 1. 进程控制块储存在 `proc[NPROC]` 数组中
   >
   > 2. `state` 成员指示了进程的状态：
   >
   >    ```c
   >    struct proc {
   >      //...
   >      enum procstate state;        // Process state
   >    };
   >    ```
   >
   > 3. 状态由 `enum procstate` 定义，共有 5 个状态：
   >
   >    ```c
   >    enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
   >    ```
   >
   >    1. `UNUSED`：当前进程未分配
   >    2. `SLEEPING`：当前进程正在暂停执行
   >    3. `RUNNABLE`：当前进程可以被分配时间片并执行
   >    4. `RUNNING`：当前进程正在执行，独占 CPU 核心
   >    5. `ZOMBIE`：当前进程是僵尸进程

4. 阅读`kernel/kalloc.c`，哪个结构体中的哪个成员可以指示空闲的内存页？Xv6中的一个页有多少字节？

   > 1. `freelist` 表示一个链表头，这个链表是未分配内存页的链表
   >
   >    ```c
   >    struct {
   >      struct spinlock lock;
   >      struct run *freelist;
   >    } kmem;
   >    ```
   >
   >    可以这样遍历可用的内存页：
   >
   >    ```c
   >      struct run *r = kmem.freelist;
   >      acquire(&kmem.lock);
   >      while (r) {
   >        // use r.
   >        r = r->next;
   >      }
   >    ```
   >
   > 2. 一个页面有 4096 字节，`PGSIZE == 4096`
   >
   >    ```c
   >    // riscv.h
   >    #define PGSIZE 4096 // bytes per page
   >    ```

5. 阅读`kernel/vm.c`，试解释`copyout()`函数各个参数的含义。

   > ```c
   > // Copy from kernel to user.
   > // Copy len bytes from src to virtual address dstva in a given page table.
   > // Return 0 on success, -1 on error.
   > int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
   >   uint64 n, va0, pa0;
   > 
   >   while (len > 0) {
   >     va0 = PGROUNDDOWN(dstva);
   >     pa0 = walkaddr(pagetable, va0);
   >     if (pa0 == 0) return -1;
   >     n = PGSIZE - (dstva - va0);
   >     if (n > len) n = len;
   >     memmove((void *)(pa0 + (dstva - va0)), src, n);
   > 
   >     len -= n;
   >     src += n;
   >     dstva = va0 + PGSIZE;
   >   }
   >   return 0;
   > }
   > ```
   >
   > `copyout` 函数用于将内核地址空间内的数据传输到用户地址空间内。参数：
   >
   > 1. `pagetable_t pagetable`：目标地址空间的页表
   > 2. `uint64 dstva`：目标地址空间的虚地址
   > 3. `char *src`：内核地址空间的源数据的地址
   > 4. `uint64 len`：传输多少个字节的数据

