#ifndef _SYS__TIMESPEC_H_
#define _SYS__TIMESPEC_H_
#include <sys/_types.h>
struct timespec {
    time_t  tv_sec;
    long    tv_nsec;
};
#endif
