#ifndef _INC_PARAM_H
#define _INC_PARAM_H
#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       10000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name

// generated from Makefile
#ifndef CPUS
#define CPUS 3
#endif

// Split kmem to every cpu cores
#define KMEM_SPLIT 1

// Split block cache locks
#define BIO_SPLIT_LOCK 1
// Split block cache into N groups
#define BIO_N 9

#ifndef COPYIN_USE_NEW
// #define COPYIN_USE_NEW 1
#endif

#ifndef EXECUTE_SHRC
// #define EXECUTE_SHRC 1
#endif

#endif  // _INC_PARAM_H