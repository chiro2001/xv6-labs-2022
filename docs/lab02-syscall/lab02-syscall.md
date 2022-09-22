## 实验 syscall

### 在系统中添加系统调用：trace

实验任务要求为添加一个 `trace` 系统调用。

1. `user.h` 中添加这个系统调用的函数声明：`int trace(int);`

2. `syscall.h` 中添加系统调用号：`*#define* SYS_trace  22`

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
2. 阅读`kernel/syscall.c`，哪些函数用于传递系统调用参数？试解释 `argraw()` 函数的含义。
3. 阅读`kernel/proc.c`和`proc.h`，进程控制块存储在哪个数组中？进程控制块中哪个成员指示了进程的状态？一共有哪些状态？
4. 阅读`kernel/kalloc.c`，哪个结构体中的哪个成员可以指示空闲的内存页？Xv6中的一个页有多少字节？
5. 阅读`kernel/vm.c`，试解释`copyout()`函数各个参数的含义。
