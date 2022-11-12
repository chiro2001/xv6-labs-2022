#include "debug.h"
#include "defs.h"
#include "elf.h"
#include "fs.h"
#include "kernel/common.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

void pkvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(pagetable, va, sz, pa, perm) != 0) panic("pkvmmap");
}

pagetable_t pkvminit() {
  pagetable_t pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);

  // uart registers
  pkvmmap(pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  pkvmmap(pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT: Donot map me! do it at allocproc
  // pkvmmap(pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  pkvmmap(pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  pkvmmap(pagetable, KERNBASE, KERNBASE, (uint64)etext - KERNBASE,
          PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  pkvmmap(pagetable, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
          PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  pkvmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return pagetable;
}

/*
 * create a direct-map page table for the kernel.
 */
void kvminit() {
  kernel_pagetable = (pagetable_t)kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart() {
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Switch h/w page table register to user's kernel pagetable,
// and enable paging.
void pkvminithart(pagetable_t pagetable) {
  w_satp(MAKE_SATP(pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk");

  // if (pagetable != kernel_pagetable)
  //   Dbg("walk user pgtbl %p for pa %p", pagetable, va);

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (!pte) {
      Panic("walk user pgtbl %p for pa %p", pagetable, va);
    }
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) {
        Dbg("Cannot allocate new page! pgtbl=%p, va=%p", pagetable, va);
        return 0;
      }
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0) return 0;
  if ((*pte & PTE_V) == 0) return 0;
  if ((*pte & PTE_U) == 0) return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kernel_pagetable, va, sz, pa, perm) != 0) panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64 kvmpa(uint64 va) {
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;

  pte = walk(kernel_pagetable, va, 0);
  if (pte == 0) panic("kvmpa");
  if ((*pte & PTE_V) == 0) panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  uint64 a, last;
  pte_t *pte;

  // if (pagetable != kernel_pagetable)
  // Dbg("user mappages: pgtb=%p va:pa = %p:%p", pagetable, va, pa);

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) {
      Dbg("walking pgtbl=%p, va=%p, alloc err", pagetable, a);
      return -1;
    }
    if (*pte & PTE_V) {
      Panic("remap: pte=%p, *pte=%p, a=%p, last=%p", *pte, pte, a, last);
      panic("remap");
    }
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// same as mappages, but ignore remap
int pkmappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
               int perm) {
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) return -1;
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// If do_free != 0, physical memory will be freed
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0) panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0) panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V) panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

void pkvmunmap(pagetable_t pagetable, uint64 va, uint64 npages) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("pkvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0) panic("pkvmunmap: walk");
    if ((*pte & PTE_V) == 0) panic("pkvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V) panic("pkvmunmap: not a leaf");
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz) {
  char *mem;

  if (sz >= PGSIZE) panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  char *mem;
  uint64 a;

  if (newsz < oldsz) return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem,
                 PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

void pkfreewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      pkfreewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      Panic("pkfreewalk: leaf, pgtbl=%p, pte=%p", pagetable, pte);
      panic("pkfreewalk: leaf");
    }
  }
  // kfree((void *)pagetable);
}

uint64 pkvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    pkvmunmap(pagetable, PGROUNDUP(newsz), npages);
  }

  return newsz;
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0) uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0) panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0) panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0) goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// Copy process's user pagetable to new kernel pagetable, will only copy flags
int pkvmcopy(pagetable_t old, pagetable_t new, uint64 sz_old, uint64 sz_new) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  // Dbg("pkvmcopy(%p, %p, %x, %x)", old, new, sz_old, sz_new);

  for (i = sz_old; i < sz_new; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0) panic("pkvmcopy: pte should exist");
    // pte_t *pte_to;
    // if ((pte_to = walk(new, i, 1)) == 0) panic("u2kvmcopy: walk fail");
    if ((*pte & PTE_V) == 0) {
      Panic("pkvmcopy: page not present! *pte = %p", *pte);
    }
    pa = PTE2PA(*pte);
    // PTE_U will prevent kernel visiting user's memory, remove PTE_U flag
    flags = PTE_FLAGS(*pte) & (~PTE_U);
    if (pkmappages(new, i, PGSIZE, pa, flags) != 0) {
      goto err;
    }
    // *pte_to = PA2PTE(pa) | flags;
  }
  return 0;

  // err..?
err:
  pkvmunmap(new, 0, i / PGSIZE);
  return -1;

  // pte_t *pte_from, *pte_to;
  // uint64 pa, i;
  // uint flags;

  // if (sz_new < sz_old) return 0;

  // sz_old = PGROUNDUP(sz_old);
  // for (i = sz_old; i < sz_new; i += PGSIZE) {
  //   if ((pte_from = walk(old, i, 0)) == 0)
  //     panic("u2kvmcopy: u_pte should exist");
  //   if ((pte_to = walk(new, i, 1)) == 0) panic("u2kvmcopy: walk fail");
  //   pa = PTE2PA(*pte_from);
  //   flags = PTE_FLAGS(*pte_from) & (~PTE_U);
  //   *pte_to = PA2PTE(pa) | flags;
  // }
  // return 0;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  // Log("copyin(table=%p, dst=%p, src=%p, len=%p)", pagetable, dst, srcva,
  // len);
#if !defined(COPYIN_USE_NEW) || 0
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
#else
  return copyin_new(pagetable, dst, srcva, len);
#endif
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  // Log("copyinstr(table=%p, dst=%p, src=%p, max=%p)", pagetable, dst, srcva,
  // max);
#if !defined(COPYIN_USE_NEW) || 0
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max) n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
#else
  return copyinstr_new(pagetable, dst, srcva, max);
#endif
}

// check if use global kpgtbl or not
int test_pagetable() {
  uint64 satp = r_satp();
  uint64 gsatp = MAKE_SATP(kernel_pagetable);
  return satp != gsatp;
}

#define PTE_VALID(pte) \
  if (!(pte & PTE_V)) continue;

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  for (uint64 vppn0 = 0; vppn0 < 256; vppn0++) {
    pte_t pte0 = pagetable[vppn0];
    PTE_VALID(pte0)
    printf("||%d: pte %p pa %p\n", vppn0, pte0, PTE2PA(pte0));
    for (uint64 vppn1 = 0; vppn1 < 512; vppn1++) {
      pte_t pte1 = ((pagetable_t)(PTE2PA(pte0)))[vppn1];
      PTE_VALID(pte1)
      printf("|| ||%d: pte %p pa %p\n", vppn1, pte1, PTE2PA(pte1));
      for (uint64 vppn2 = 0; vppn2 < 512; vppn2++) {
        pte_t pte2 = ((pagetable_t)(PTE2PA(pte1)))[vppn2];
        PTE_VALID(pte2)
        printf("|| || ||%d: pte %p pa %p\n", vppn2, pte2, PTE2PA(pte2));
      }
    }
  }
}