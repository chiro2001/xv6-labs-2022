#ifndef _INC_DATE_H
#define _INC_DATE_H
#include "kernel/common.h"
struct rtcdate {
  uint second;
  uint minute;
  uint hour;
  uint day;
  uint month;
  uint year;
};
#endif