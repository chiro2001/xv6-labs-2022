
#ifndef _KERNEL_DBGCONF_H
#define _KERNEL_DBGCONF_H

// #define RUN_GRADE 1

#ifndef RUN_GRADE

#ifndef DEBUG
// #define DEBUG 1
#endif

#ifndef CONFIG_PRINT_LOG
#define CONFIG_PRINT_LOG 1
#endif

#ifndef CONFIG_PRINT_ERR_LOG
#define CONFIG_PRINT_ERR_LOG 1
#endif

#ifndef EXECUTE_SHRC
#define EXECUTE_SHRC 1
#endif

#else

#ifdef DEBUG
#undef DEBUG
#endif

#ifdef CONFIG_PRINT_LOG
#undef CONFIG_PRINT_LOG
#endif

#ifdef CONFIG_PRINT_ERR_LOG
#undef CONFIG_PRINT_ERR_LOG
#endif

#ifdef EXECUTE_SHRC
#undef EXECUTE_SHRC
#endif

#endif

// #ifndef DEBUG_SYS_TRACE
// #define DEBUG_SYS_TRACE 1
// #endif

#endif  // _KERNEL_DBGCONF_H