#include "defs.h"
#include "kernel/common.h"
#include "param.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

//
// This file contains copyin_new() and copyinstr_new(), the
// replacements for copyin and coyinstr in vm.c.
//

static struct stats {
  int ncopyin;
  int ncopyinstr;
} stats;

int statscopyin(char *buf, int sz) {
  int n;
  n = snprintf(buf, sz, "copyin: %d\n", stats.ncopyin);
  n += snprintf(buf + n, sz, "copyinstr: %d\n", stats.ncopyinstr);
  return n;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  struct proc *p = myproc();

  if (srcva >= p->sz || srcva + len >= p->sz || srcva + len < srcva) {
    Dbg("copyin_new: invalid args! dst=%p, srcva=%p, len=%d, p->sz=%d", dst, srcva, len, p->sz);
    if (srcva + len >= p->sz) {
      Dbg("srcva + len >= p->sz!, srcva(%d) + len(%d) >= p->sz(%d)", srcva, len, p->sz);
    }
    // vmprint(pagetable);
    return -1;
  }
  // uint64 va0, pa0;
  // va0 = PGROUNDDOWN(srcva);
  // pa0 = walkaddr(pagetable, va0);
  // printf("dst=%p, pa0=%p\n", dst, pa0);
  memmove((void *)dst, (void *)srcva, len);
  stats.ncopyin++;  // XXX lock
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  struct proc *p = myproc();
  char *s = (char *)srcva;

  stats.ncopyinstr++;  // XXX lock
  for (int i = 0; i < max && srcva + i < p->sz; i++) {
    dst[i] = s[i];
    if (s[i] == '\0') return 0;
  }
  return -1;
}
