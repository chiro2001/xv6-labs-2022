#ifndef _INC_SYSINFO_H
#define _INC_SYSINFO_H
struct sysinfo {
  uint64 freemem;   // amount of free memory (bytes)
  uint64 nproc;     // number of process
  uint64 freefd;    // number of free file descriptor
};
#endif  // _INC_SYSINFO_H