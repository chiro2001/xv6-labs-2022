// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "buf.h"
#include "defs.h"
#include "fs.h"
#include "kernel/common.h"
#include "param.h"
#include "riscv.h"
#include "sleeplock.h"
#include "spinlock.h"
#include "types.h"

// print log
// #define BIO_LOG 1

#ifdef BIO_LOG
#define LOCK_GRP_LOG Log("\t  LOCK_GRP(%d)", hash)
#define UNLOCK_GRP_LOG Log("\tUNLOCK_GRP(%d)", hash)
#else
#define LOCK_GRP_LOG
#define UNLOCK_GRP_LOG
#endif
#define DEF_HASH IFDEF(BIO_SPLIT_LOCK, int hash = b->blockno % BIO_N)

#define LOCK_ALL_F acquire(&bcache.lock)
#define UNLOCK_ALL_F release(&bcache.lock)

#define OPHASH IFDEF(BIO_SPLIT_LOCK, [hash])

#define LOCK_ALL IFNDEF(BIO_SPLIT_LOCK, LOCK_ALL_F)
#define UNLOCK_ALL IFNDEF(BIO_SPLIT_LOCK, UNLOCK_ALL_F)

#define LOCK_GRP                                               \
  do {                                                         \
    IFDEF(BIO_SPLIT_LOCK, acquire(&bcache.lock_bucket[hash])); \
    LOCK_GRP_LOG;                                              \
  } while (0)
#define UNLOCK_GRP                                             \
  do {                                                         \
    UNLOCK_GRP_LOG;                                            \
    IFDEF(BIO_SPLIT_LOCK, release(&bcache.lock_bucket[hash])); \
  } while (0)

struct {
  struct spinlock lock;
  IFDEF(BIO_SPLIT_LOCK, struct spinlock lock_bucket[BIO_N]);
  struct buf buf IFDEF(BIO_SPLIT_LOCK, [BIO_N])[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head IFDEF(BIO_SPLIT_LOCK, [BIO_N]);
} bcache;

void binit(void) {
  initlock(&bcache.lock, "bcache");
#ifdef BIO_SPLIT_LOCK
  char name_grp[256];
  for (int i = 0; i < BIO_N; i++) {
    snprintf(name_grp, sizeof(name_grp), "bcache_bucket_%d", i);
    // Log("init lock %s", name_grp);
    initlock(&bcache.lock_bucket[i], name_grp);
  }
#endif

  // Create linked list of buffers
  struct buf *b;
  /*
   *             ┌───────┐ ┌──────┐ ┌──────┐  ┌────►&head
   *         prev│       │ │      │ │      │  │
   *        ┌────┴───┬───▼─┴──┬───▼─┴──┬───▼──┴─┐
   *        │ 0      │ 1      │ 2      │ 3      │
   * buf    │        │        │        │        │
   *        │        │        │        │        │
   *        │        │        │        │        │
   *        └──┬──▲──┴───┬──▲─┴───┬──▲─┴───┬────┘
   *           │  │ next │  │     │  │     │
   * &head◄────┘  └──────┘  └─────┘  └─────┘
   */
#ifdef BIO_SPLIT_LOCK
  // generate BIO_N empty linked list
  for (int hash = 0; hash < BIO_N; hash++) {
#endif
    bcache.head OPHASH.prev = &bcache.head OPHASH;
    bcache.head OPHASH.next = &bcache.head OPHASH;
    for (b = bcache.buf OPHASH; b < bcache.buf OPHASH + NBUF; b++) {
      b->next = bcache.head OPHASH.next;
      b->prev = &bcache.head OPHASH;
      initsleeplock(&b->lock, "buffer");
      bcache.head OPHASH.next->prev = b;
      bcache.head OPHASH.next = b;
    }
#ifdef BIO_SPLIT_LOCK
  }
#endif
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  LOCK_ALL;

  IFDEF(BIO_SPLIT_LOCK, int hash = blockno % BIO_N);
  // Is the block already cached?
  struct buf *iter;
  for (b = bcache.head OPHASH.next; b != &bcache.head OPHASH; b = iter) {
    LOCK_GRP;
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      struct sleeplock *lock = &b->lock;
      UNLOCK_GRP;
      UNLOCK_ALL;
      acquiresleep(lock);
      return b;
    }
    iter = b->next;
    Assert(iter != b, "Dead loop! ticks: %d", ticks);
    UNLOCK_GRP;
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for (b = bcache.head OPHASH.prev; b != &bcache.head OPHASH; b = iter) {
    LOCK_GRP;
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      struct sleeplock *lock = &b->lock;
      UNLOCK_GRP;
      UNLOCK_ALL;
      acquiresleep(lock);
      return b;
    }
    iter = b->prev;
    Assert(iter != b, "Dead loop! ticks: %d", ticks);
    UNLOCK_GRP;
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("brelse");

  releasesleep(&b->lock);

  DEF_HASH;
  LOCK_ALL;
  LOCK_GRP;
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head OPHASH.next;
    b->prev = &bcache.head OPHASH;
    bcache.head OPHASH.next->prev = b;
    bcache.head OPHASH.next = b;
  }
  UNLOCK_ALL;
  UNLOCK_GRP;
}

void bpin(struct buf *b) {
  DEF_HASH;
  LOCK_GRP;
  LOCK_ALL;
  b->refcnt++;
  UNLOCK_ALL;
  UNLOCK_GRP;
}

void bunpin(struct buf *b) {
  DEF_HASH;
  LOCK_GRP;
  LOCK_ALL;
  b->refcnt--;
  UNLOCK_ALL;
  UNLOCK_GRP;
}
