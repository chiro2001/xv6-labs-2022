#ifndef _INC_BUF_H
#define _INC_BUF_H
#include "kernel/common.h"
#include "sleeplock.h"
struct buf {
  int valid;  // has data been read from disk?
  int disk;   // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev;  // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};
#endif