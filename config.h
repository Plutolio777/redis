#ifndef __CONFIG_H
#define __CONFIG_H

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

/* test for malloc_size() */
#ifdef __APPLE__ // mac系统适配
#include <malloc/malloc.h>
#define HAVE_MALLOC_SIZE 1
#define redis_malloc_size(p) malloc_size(p)
#endif

/* define redis_fstat to fstat or fstat64() */
#if defined(__APPLE__) && !defined(MAC_OS_X_VERSION_10_6)
#define redis_fstat fstat64
#define redis_stat stat64
#else
#define redis_fstat fstat
#define redis_stat stat
#endif

/* test for backtrace() */
#if defined(__APPLE__) || defined(__linux__)
#define HAVE_BACKTRACE 1
#endif

/* test for polling API */
// 这里比较关键 这里是不同系统的网络模型适配
// 如果是linux系统则标志有epoll
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

// 如果是linux系统则标志有kqueue
#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#define HAVE_KQUEUE 1
#endif

#endif
