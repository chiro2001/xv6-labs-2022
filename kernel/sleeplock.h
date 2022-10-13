#ifndef _INC_SLEEP_CLOCK_H
#define _INC_SLEEP_CLOCK_H
// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};

#endif  // _INC_SLEEP_CLOCK_H