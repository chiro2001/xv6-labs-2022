#ifndef _INC_SPINLOCK_H
#define _INC_SPINLOCK_H
#include "kernel/common.h"
// Mutual exclusion lock.
struct spinlock {
  uint locked;  // Is the lock held?

  // For debugging:
  char *name;       // Name of lock.
  struct cpu *cpu;  // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#endif  // _INC_SPINLOCK_H