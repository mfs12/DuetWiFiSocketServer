#ifndef DEBUG_H
#define DEBUG_H 1

#include <cstdio>

#if DEBUG
#define debug(fmt, args...)	do { printf("DBG %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#else
#define debug(_format, ...)	do {} while(false)
#endif

#define info(fmt, args...)	do { printf("INFO %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#define warn(fmt, args...)	do { printf("WARN %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)
#define err(fmt, args...)	do { printf("ERR %s(%d): " fmt, __FILE__, __LINE__, ##args); } while (0)

#endif /* ifndef DEBUG_H */
