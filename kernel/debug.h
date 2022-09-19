#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "utils.h"

#define Log(format, ...)                                                    \
  _Log(CONFIG_PRINT_LOG, ANSI_FMT("[%s:%d %s] " format, ANSI_FG_BLUE) "\n", \
       __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define Err(format, ...)                                                 \
  _Err(CONFIG_PRINT_LOG,                                                 \
       ANSI_FMT("[%s:%d %s ERROR] " format, ANSI_FG_RED) "\n", __FILE__, \
       __LINE__, __func__, ##__VA_ARGS__)

#define Dbg(format, ...)                                                   \
  _Log(DEBUG, ANSI_FMT("[%s:%d %s] " format, ANSI_FG_BLUE) "\n", __FILE__, \
       __LINE__, __func__, ##__VA_ARGS__)

#define printt(module, format, ...)                                     \
  IFDEF(concat(CONFIG_, module),                                        \
        _Log(CONFIG_PRINT_TRACE,                                        \
             ANSI_FMT("[%12lu][%08x][%8s] ", ANSI_FG_CYAN) format "\n", \
             g_nr_guest_instr, cpu.pc, str(module), ##__VA_ARGS__))

#define printd(module, format, ...)                                          \
  IFDEF(                                                                     \
      concat3(CONFIG_, module, _EN), do {                                    \
        time_t raw_time = get_time_sec();                                    \
        struct tm *ptminfo = localtime(&raw_time);                           \
        _Err(CONFIG_PRINT_DEVICE,                                            \
             ANSI_FMT("[%12lu][%02d-%02d-%02d %02d:%02d:%02d][%6s] ",        \
                      ANSI_FG_GREEN) format "\n",                            \
             g_nr_guest_instr, ptminfo->tm_year + 1900, ptminfo->tm_mon + 1, \
             ptminfo->tm_mday, ptminfo->tm_hour, ptminfo->tm_min,            \
             ptminfo->tm_sec, str(module), ##__VA_ARGS__);                   \
      } while (0))
#define Assert(cond, format, ...)                             \
  do {                                                        \
    if (!(cond)) {                                            \
      Err(ANSI_FMT(format, ANSI_FG_RED) "\n", ##__VA_ARGS__); \
      for (;;)                                                \
        ;                                                     \
    }                                                         \
  } while (0)

#define Panic(format, ...) Assert(0, format, ##__VA_ARGS__)

#define TODO() Panic("please implement me")

#endif
