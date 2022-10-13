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

// #define BIO_LOG 1

#if 0
#define LOCK_BUF                                                         \
  do {                                                                   \
    IFDEF(BIO_SPLIT_LOCK, acquire(&bcache.lock_bucket[b - bcache.buf])); \
  } while (0)
#define UNLOCK_BUF                                                       \
  do {                                                                   \
    IFDEF(BIO_SPLIT_LOCK, release(&bcache.lock_bucket[b - bcache.buf])); \
  } while (0)
#endif

#define LOCK_BUF_LOG IFDEF(BIO_LOG, Log("\t  LOCK_BUF(%d)", b - bcache.buf))
#define UNLOCK_BUF_LOG IFDEF(BIO_LOG, Log("\tUNLOCK_BUF(%d)", b - bcache.buf))

#ifndef BIO_SPLIT_LOCK
// #define LOCK_BUF Log("\t  LOCK_BUF(%d)", b - bcache.buf)
// #define UNLOCK_BUF Log("\tUNLOCK_BUF(%d)", b - bcache.buf)
#define LOCK_BUF
#define UNLOCK_BUF
#define LOCK_ALL acquire(&bcache.lock)
#define UNLOCK_ALL release(&bcache.lock)
#else
#define LOCK_BUF                                                   \
  do {                                                             \
    IFDEF(BIO_SPLIT_LOCK,                                          \
          acquire(&bcache.lock_bucket[(b - bcache.buf) % BIO_N])); \
    IFDEF(BIO_SPLIT_LOCK, LOCK_BUF_LOG);                           \
  } while (0)
#define UNLOCK_BUF                                                 \
  do {                                                             \
    IFDEF(BIO_SPLIT_LOCK, UNLOCK_BUF_LOG);                         \
    IFDEF(BIO_SPLIT_LOCK,                                          \
          release(&bcache.lock_bucket[(b - bcache.buf) % BIO_N])); \
  } while (0)
#define LOCK_ALL IFNDEF(BIO_SPLIT_LOCK, acquire(&bcache.lock))
#define UNLOCK_ALL IFNDEF(BIO_SPLIT_LOCK, release(&bcache.lock))
#endif

#define BIO_LOCK_BUCKET (NBUF + 1)
// #define BIO_N 1
#define BIO_N BIO_LOCK_BUCKET

struct {
  struct spinlock lock;
  IFDEF(BIO_SPLIT_LOCK, struct spinlock lock_bucket[BIO_LOCK_BUCKET]);
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void binit(void) {
  struct buf *b;

  initlock(&bcache.lock, "bcache");
#ifdef BIO_SPLIT_LOCK
  char name_buf[256];
  for (int i = 0; i < BIO_LOCK_BUCKET; i++) {
    snprintf(name_buf, sizeof(name_buf), "bcache_bucket_%d", i);
    Log("init lock %s", name_buf);
    // initlock(&bcache.lock_bucket[i], "bcache_bucket");
    initlock(&bcache.lock_bucket[i], name_buf);
  }
#endif

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  LOCK_ALL;

  // Is the block already cached?
  struct buf *iter;
  for (b = bcache.head.next; b != &bcache.head; b = iter) {
    LOCK_BUF;
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      struct sleeplock *lock = &b->lock;
      UNLOCK_BUF;
      UNLOCK_ALL;
      acquiresleep(lock);
      return b;
    }
    iter = b->next;
    UNLOCK_BUF;
  }

  // Log("bget(%d, %d): no cached", dev, blockno);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
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

  LOCK_ALL;
  LOCK_BUF;
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  UNLOCK_ALL;
  UNLOCK_BUF;
}

void bpin(struct buf *b) {
  LOCK_BUF;
  LOCK_ALL;
  b->refcnt++;
  UNLOCK_ALL;
  UNLOCK_BUF;
}

void bunpin(struct buf *b) {
  LOCK_BUF;
  LOCK_ALL;
  b->refcnt--;
  UNLOCK_ALL;
  UNLOCK_BUF;
}
