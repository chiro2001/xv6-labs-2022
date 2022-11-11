#include "defs.h"
#include "kernel/common.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

volatile static int started = 0;
volatile static int harts_started[CPUS] = {0};

// start() jumps here in supervisor mode on all CPUs.
void main() {
  if (cpuid() == 0) {
    consoleinit();
#if defined(LAB_PGTBL) || defined(LAB_LOCK)
    statsinit();
#endif
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();             // physical page allocator
    kvminit();           // create kernel page table
    kvminithart();       // turn on paging
    procinit();          // process table
    trapinit();          // trap vectors
    trapinithart();      // install kernel trap vector
    plicinit();          // set up interrupt controller
    plicinithart();      // ask PLIC for device interrupts
    binit();             // buffer cache
    iinit();             // inode cache
    fileinit();          // file table
    virtio_disk_init();  // emulated hard disk
#ifdef LAB_NET
    pci_init();
    sockinit();
#endif
    userinit();  // first user process
    __sync_synchronize();
    for (int i = 0; i < CPUS; i++) harts_started[i] = 0;
    harts_started[0] = 1;
    started = 1;
    int harts_inits_done = 0;
    while (!harts_inits_done) {
      int not_ready = 0;
      for (int i = 1; i < CPUS; i++) {
        if (!harts_started[i]) {
          not_ready = 1;
          break;
        }
      }
      if (!not_ready) harts_inits_done = 1;
    }
    Log("Init done with %d CPUS", CPUS);
  } else {
    while (started == 0)
      ;
    // also init kmem
    kinit();
    __sync_synchronize();
    printf("hart %d starting [%d/%d]\n", cpuid(), cpuid() + 1, CPUS);
    kvminithart();   // turn on paging
    trapinithart();  // install kernel trap vector
    plicinithart();  // ask PLIC for device interrupts
    // tell cpu0 that i have inited
    harts_started[cpuid()] = 1;
  }

  scheduler();
}
