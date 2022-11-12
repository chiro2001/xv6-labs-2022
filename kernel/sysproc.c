#include "date.h"
#include "debug.h"
#include "defs.h"
#include "kernel/common.h"
#include "memlayout.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "syscall.h"
#include "sysinfo.h"
#include "types.h"

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
  if (n > 0) {
    struct proc *p = myproc();
    pkvmcopy(p->pagetable, p->k_pagetable, addr, addr + n);
  }
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

uint64 sys_checkvm() { return (uint64)test_pagetable(); }

struct sys_trace_info sys_trace_info = {.p = 0, .mask = 0};

// asserts: pid never overflows
int sys_trace_child_pids[1024];
int sys_trace_child_pids_tail = 0;

uint64 sys_trace(void) {
  if (sys_trace_info.p) return -1;
  sys_trace_info.p = myproc();
  argint(0, &sys_trace_info.mask);
  // printf("pid %d tracing %d...\n", sys_trace_info.p->pid,
  // sys_trace_info.mask);
  if (SYS_trace & sys_trace_info.mask)
    printf("%d: sys_%s(%d) -> %d\n", sys_trace_info.p->pid,
           syscall_names[SYS_trace], sys_trace_info.p->trapframe->a0, 0);
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
  struct proc *p = myproc();
  struct sysinfo *d;
  struct sysinfo st;
  if (argaddr(0, (uint64 *)&d) < 0) return -1;
  // printf("\tsysinfo with addr: %p\n", d);
  // check bad address
  int is_bad_addr = 0;
  uint64 tmp;
  for (uint8 i = 0; i < sizeof(struct sysinfo) / sizeof(uint64); i++) {
    uint64 addr = (uint64)d + sizeof(uint64) * i;
    if (fetchaddr(addr, &tmp) < 0) {
      is_bad_addr = 1;
      // printf("\tbad addr: %p\n", addr);
      break;
    }
  }
  if (is_bad_addr) return -1;
  st.freemem = kpagefree() * PGSIZE;
  st.nproc = procn();
  st.freefd = fdfree();
  Dbg("sysinfo { freemem=%x, nproc=%d, freefd=%d }", st.freemem, st.nproc,
      st.freefd);
  copyout(p->pagetable, (uint64)d, (char *)&st, sizeof(st));
  return 0;
}