// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void kinit() {
  initlock(&kmem[cpuid()].lock, "kmem");
  uint64 size = ((uint64)PHYSTOP - (uint64)end) / CPUS;
  freerange(end + size * cpuid(), end + size * (cpuid() + 1));
  printf(
      "KMEM: [%p - %p], cpu %d [%p - %p], total %x PAGES, PGSIZE %x, cpu %x "
      "PAGES\n",
      end, PHYSTOP, cpuid(), end + size * cpuid(), end + size * (cpuid() + 1),
      ((uint64)PHYSTOP - (uint64)end) / PGSIZE, PGSIZE, size / PGSIZE);
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

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  int cid = cpuid();
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

  int cid = cpuid();
  acquire(&kmem[cid].lock);
  // if (cid != 0)
  //   r = kmem[cid].freelist;
  // else r = 0;
  r = kmem[cid].freelist;
  if (r) kmem[cid].freelist = r->next;
  release(&kmem[cid].lock);
  if (!r) {
    // "steal" part of other CPU's freelist
    // lock in order
    for (int i = 0; i < CPUS; i++)
      acquire(&kmem[i].lock);
    for (int i = 0; i < CPUS; i++) {
      r = kmem[i].freelist;
      if (r) {
        kmem[i].freelist = r->next;
        break;
      }
    }
    for (int i = CPUS - 1; i >= 0; i--)
      release(&kmem[i].lock);
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
  int cid = cpuid();
  struct run *r = kmem[cid].freelist;
  acquire(&kmem[cid].lock);
  while (r) {
    n++;
    r = r->next;
  }
  release(&kmem[cid].lock);
  return n;
}