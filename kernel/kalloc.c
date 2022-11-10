// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "defs.h"
#include "kernel/debug.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "types.h"

#define KMEM_CPUS MUXDEF(KMEM_SPLIT, CPUS, 1)

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void freerange(void *pa_start, void *pa_end);

void kinit() {
  if (cpuid() != 0) {
    IFNDEF(KMEM_SPLIT, return );
  }
  const char lock_name_const[] = "kmem_hart ";
  char lock_name[sizeof(lock_name_const)];
  for (int i = 0; i < sizeof(lock_name_const); i++) {
    lock_name[i] = lock_name_const[i];
  }
  int cid = MUXDEF(KMEM_SPLIT, cpuid(), 0);
  IFDEF(KMEM_SPLIT, lock_name[9] = cid + '0');
  initlock(&kmem[cid].lock, lock_name);
  Log("cpu[%d] lock init: %s; cpu %d", cid, lock_name, cid);
  uint64 size = ((uint64)PHYSTOP - (uint64)end) / KMEM_CPUS;
  freerange(end + size * cid, end + size * (cid + 1));
  Log("KMEM: [%p - %p], cpu %d [%p - %p], PAGES [%x/%x]", end, PHYSTOP, cid,
      end + size * cid, end + size * (cid + 1), size / PGSIZE,
      ((uint64)PHYSTOP - (uint64)end) / PGSIZE);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP) {
    Panic("kfree: ((uint64)pa % PGSIZE) == %x (!= 0 ?) || pa == %x < end ? || > PHYSTOP ?", ((uint64)pa % PGSIZE), pa);
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  int cid = MUXDEF(KMEM_SPLIT, cpuid(), 0);
  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  int cid = MUXDEF(KMEM_SPLIT, cpuid(), 0);
  acquire(&kmem[cid].lock);
  r = kmem[cid].freelist;
  if (r) kmem[cid].freelist = r->next;
  release(&kmem[cid].lock);
  if (!r) {
    // "steal" part of other CPU's freelist
    // lock in order
    for (int i = 0; i < KMEM_CPUS; i++) acquire(&kmem[i].lock);
    for (int i = 0; i < KMEM_CPUS; i++) {
      r = kmem[i].freelist;
      if (r) {
        kmem[i].freelist = r->next;
        break;
      }
    }
    for (int i = KMEM_CPUS - 1; i >= 0; i--) release(&kmem[i].lock);
  }

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}

// Get how many pages have been allocated
uint32 kpageused(void) {
  return ((PHYSTOP - (uint64)end) / PGSIZE) - kpagefree();
}

// Get how many free pages
uint32 kpagefree(void) {
  int n = 0;
  int cid = MUXDEF(KMEM_SPLIT, cpuid(), 0);
  struct run *r = kmem[cid].freelist;
  acquire(&kmem[cid].lock);
  while (r) {
    n++;
    r = r->next;
  }
  release(&kmem[cid].lock);
  return n;
}