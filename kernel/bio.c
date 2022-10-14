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

#define BIO_SPLIT_LOCK 1

#define BIO_LOG 1

#define BIO_N 19
// #define BIO_N 1

#define LOCK_BUF_LOG IFDEF(BIO_LOG, Log("\t  LOCK_BUF(%d)", blockno % BIO_N))
#define UNLOCK_BUF_LOG IFDEF(BIO_LOG, Log("\tUNLOCK_BUF(%d)", blockno % BIO_N))

#define LOCK_ALL_F acquire(&bcache.lock)
#define UNLOCK_ALL_F release(&bcache.lock)

#ifndef BIO_SPLIT_LOCK
#define LOCK_BUF
#define UNLOCK_BUF
#define LOCK_ALL LOCK_ALL_F
#define UNLOCK_ALL UNLOCK_ALL_F
#else
#define LOCK_BUF                                                          \
  do {                                                                    \
    IFDEF(BIO_SPLIT_LOCK, acquire(&bcache.lock_bucket[blockno % BIO_N])); \
    IFDEF(BIO_SPLIT_LOCK, LOCK_BUF_LOG);                                  \
  } while (0)
#define UNLOCK_BUF                                                        \
  do {                                                                    \
    IFDEF(BIO_SPLIT_LOCK, UNLOCK_BUF_LOG);                                \
    IFDEF(BIO_SPLIT_LOCK, release(&bcache.lock_bucket[blockno % BIO_N])); \
  } while (0)
#define LOCK_ALL IFNDEF(BIO_SPLIT_LOCK, acquire(&bcache.lock))
#define UNLOCK_ALL IFNDEF(BIO_SPLIT_LOCK, release(&bcache.lock))
#endif

struct {
  struct spinlock lock;
  IFDEF(BIO_SPLIT_LOCK, struct spinlock lock_bucket[BIO_N]);
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  MUXDEF(BIO_SPLIT_LOCK, struct buf head[BIO_N], struct buf head);
} bcache;

void binit(void) {
  initlock(&bcache.lock, "bcache");
#ifdef BIO_SPLIT_LOCK
  char name_buf[256];
  for (int i = 0; i < BIO_N; i++) {
    snprintf(name_buf, sizeof(name_buf), "bcache_bucket_%d", i);
    Log("init lock %s", name_buf);
    // initlock(&bcache.lock_bucket[i], "bcache_bucket");
    initlock(&bcache.lock_bucket[i], name_buf);
  }
#endif

  // Create linked list of buffers
  struct buf *b;
#ifdef BIO_SPLIT_LOCK
  for (int i = 0; i < BIO_N; i++) {
    bcache.head[i].prev = 0;
    bcache.head[i].next = 0;
  }
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = 0;
    b->prev = 0;
    initsleeplock(&b->lock, "buffer");
  }
#else
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
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
  for (b = bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]).next;
       b != &bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]) && b; b = iter) {
    LOCK_BUF;
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      struct sleeplock *lock = &b->lock;
      // Log("bget(%d, %d): cached", dev, blockno);
      UNLOCK_BUF;
      UNLOCK_ALL;
      acquiresleep(lock);
      return b;
    }
    iter = b->next;
    Assert(b != b->next, "Infty loop! %d", b - bcache.buf);
    // Log("next - b = %d", b->next - b);
    UNLOCK_BUF;
  }

  // Log("bget(%d, %d): no cached", dev, blockno);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
#ifndef BIO_SPLIT_LOCK
  for (b = bcache.head.prev; b != &bcache.head; b = iter) {
    LOCK_BUF;
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      struct sleeplock *lock = &b->lock;
      UNLOCK_BUF;
      UNLOCK_ALL;
      acquiresleep(lock);
      return b;
    }
    iter = b->prev;
    UNLOCK_BUF;
  }
#else
  LOCK_ALL_F;
  // find all buf that's free
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    if (b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      // add to hashed link
      b->next = bcache.head[hash].next;
      b->prev = &bcache.head[hash];
      bcache.head[hash].next->prev = b;
      bcache.head[hash].next = b;
      struct sleeplock *lock = &b->lock;
      UNLOCK_ALL_F;
      acquiresleep(lock);
      return b;
    }
  }
  UNLOCK_ALL_F;
#endif
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

  IFDEF(BIO_SPLIT_LOCK, int blockno = b->blockno);
  IFDEF(BIO_SPLIT_LOCK, int hash = blockno % BIO_N);
  LOCK_ALL;
  LOCK_BUF;
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]).next;
    b->prev = &bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]);
    bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]).next->prev = b;
    bcache.head IFDEF(BIO_SPLIT_LOCK, [hash]).next = b;
  }

  UNLOCK_ALL;
  UNLOCK_BUF;
}

void bpin(struct buf *b) {
  IFDEF(BIO_SPLIT_LOCK, int blockno = b->blockno);
  LOCK_BUF;
  LOCK_ALL;
  b->refcnt++;
  UNLOCK_ALL;
  UNLOCK_BUF;
}

void bunpin(struct buf *b) {
  IFDEF(BIO_SPLIT_LOCK, int blockno = b->blockno);
  LOCK_BUF;
  LOCK_ALL;
  b->refcnt--;
  UNLOCK_ALL;
  UNLOCK_BUF;
}
