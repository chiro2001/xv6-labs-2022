#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_wait(void) {
  uint64 p;
  if (argaddr(0, &p) < 0) return -1;
  return wait(p);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

struct sys_trace_info sys_trace_info = {
  .p = 0,
  .mask = 0
};

// 因为 pid 总是自增的，这样做大致没有问题。
// fixme: sys_trace_pids when pid overflow
int sys_trace_child_pids[1024];
int sys_trace_child_pids_tail = 0;

uint64 sys_trace(void) {
  if (sys_trace_info.p) return -1;
  sys_trace_info.p = myproc();
  argint(0, &sys_trace_info.mask);
  // printf("pid %d tracing %d...\n", sys_trace_info.p->pid, sys_trace_info.mask);
  printf("%d: syscall %s -> %d\n", sys_trace_info.p->pid, syscall_names[SYS_trace], sys_trace_info.p->trapframe->a0);
  sys_trace_child_pids[0] = sys_trace_info.p->pid;
  sys_trace_child_pids_tail = 1;
  return 0;
}

uint64 sys_fork(void) {
  struct proc *p = myproc();
  struct proc *tracer = p;
  int pid_child = fork();
  while (tracer && sys_trace_info.p) {
    if (tracer->pid == sys_trace_info.p->pid) {
      sys_trace_child_pids[sys_trace_child_pids_tail++] = pid_child;
      break;
    }
    tracer = tracer->parent;
  }
  return pid_child;
}

uint64 sys_sysinfo(void) {
  return 0;
}