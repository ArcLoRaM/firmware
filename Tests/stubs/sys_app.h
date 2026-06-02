/* Tests/stubs/sys_app.h — host-test stub, no platform dependencies */
#ifndef __SYS_APP_H__
#define __SYS_APP_H__

/* Trace level constants (mirrors stm32_adv_trace.h) */
#define VLEVEL_ALWAYS  0
#define VLEVEL_L       1
#define VLEVEL_M       2
#define VLEVEL_H       3

/* Timestamp flag constants */
#define TS_OFF         0
#define TS_ON          1
#define T_REG_OFF      0

/* No-op logging macros */
#define APP_PRINTF(...)          do { } while (0)
#define APP_LOG(TS, VL, ...)     do { } while (0)

#endif /* __SYS_APP_H__ */
