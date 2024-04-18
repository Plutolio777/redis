/* zmalloc - total amount of allocated memory aware version of malloc()
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h> // malloc() realloc() free()都是定义在这个里面的
#include <string.h>
#include <pthread.h>
#include "config.h" // 这个里面会定义一些配置信息

#if defined(__sun)
#define PREFIX_SIZE sizeof(long long)
#else
#define PREFIX_SIZE sizeof(size_t)
#endif

/*
 * 下面这两个方法是用来变更使用内存变量的
 * 有两种方式 使用@zmalloc_thread_safe 变量修改前是否加锁
 */
#define increment_used_memory(_n) do { \
    if (zmalloc_thread_safe) { \
        pthread_mutex_lock(&used_memory_mutex);  \
        used_memory += _n; \
        pthread_mutex_unlock(&used_memory_mutex); \
    } else { \
        used_memory += _n; \
    } \
} while(0)

#define decrement_used_memory(_n) do { \
    if (zmalloc_thread_safe) { \
        pthread_mutex_lock(&used_memory_mutex);  \
        used_memory -= _n; \
        pthread_mutex_unlock(&used_memory_mutex); \
    } else { \
        used_memory -= _n; \
    } \
} while(0)

static size_t used_memory = 0; // 静态变量
static int zmalloc_thread_safe = 0; // 线程安全标志
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER; // 互斥锁

/**
 * 分配内存时OOM的异常处理方式
 * @param size
 */
static void zmalloc_oom(size_t size) {
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

/**
 * zmalloc
 * 分配 size 大小的内存空间
 * @param size 需要分配的内存大小
 * @return 已分配内存的指针
 */
void *zmalloc(size_t size) {
    // 调用C的malloc函数进行内存分配
    // 为什么分配内存的时候需要多分配一个PREFIX_SIZE
    void *ptr = malloc(size+PREFIX_SIZE);
    // 如果指针为空 则说明分配异常 抛出OOM异常
    if (!ptr) zmalloc_oom(size);
#ifdef HAVE_MALLOC_SIZE
    increment_used_memory(redis_malloc_size(ptr));
    return ptr;
#else
    // 实际上PREFIX_SIZE多分配的内存时为了存储这个内存空间的大小
    *((size_t*)ptr) = size;
    // 计算使用内存
    increment_used_memory(size+PREFIX_SIZE);
    // 返回数据真正地址的指针
    //         ptr
    //          |
    // <size_t> V <--------   size ------------->
    // [{ size } { data  data data data         }]
    return (char*)ptr+PREFIX_SIZE;
#endif
}

/**
 * zrealloc
 * 在分配的地址后再重新增加size大小的内存空间
 * @param ptr
 * @param size
 * @return
 */
void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;
    // 如果地址指针为空直接分配内存即可
    if (ptr == NULL) return zmalloc(size);
#ifdef HAVE_MALLOC_SIZE
    oldsize = redis_malloc_size(ptr);
    newptr = realloc(ptr,size);
    if (!newptr) zmalloc_oom(size);

    decrement_used_memory(oldsize);
    increment_used_memory(redis_malloc_size(newptr));
    return newptr;
#else
    // 回到真正的地址头部 这样的话就可以获得之前内存地址的大小了
    realptr = (char*)ptr-PREFIX_SIZE;
    // 获取就得内存大小
    oldsize = *((size_t *) realptr);
    // 调用系统方法重新分配内存
    newptr = realloc(realptr,size+PREFIX_SIZE);
    // 如果分配内存时报 抛出OOM异常
    if (!newptr) zmalloc_oom(size);
    // 赋值新的大小
    *((size_t*)newptr) = size;
    // 调整系统已使用内存带下
    decrement_used_memory(oldsize);
    increment_used_memory(size);
    // 返回地址
    return (char*)newptr+PREFIX_SIZE;
#endif
}


/**
 * 释放内存的方法
 * @param ptr
 */
void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif
    // 如果指针为0无需释放直接返回
    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    decrement_used_memory(redis_malloc_size(ptr));
    free(ptr);
#else
    // 左移获取真正地址
    realptr = (char*)ptr-PREFIX_SIZE;
    // 调整系统使用内存大小
    oldsize = *((size_t*)realptr);
    decrement_used_memory(oldsize+PREFIX_SIZE);
    // 系统调用释放内存
    free(realptr);
#endif
}

/*
 *
 * 根据字符串长度开辟内存 为了方便创建字符串内存
 */
char *zstrdup(const char *s) {
    // 内存大小+1是为了放结尾符
    size_t l = strlen(s)+1;
    char *p = zmalloc(l);

    memcpy(p,s,l);
    return p;
}

/*
 * 获取目前已使用的总内存
 */
size_t zmalloc_used_memory(void) {
    size_t um;

    if (zmalloc_thread_safe) pthread_mutex_lock(&used_memory_mutex);
    um = used_memory;
    if (zmalloc_thread_safe) pthread_mutex_unlock(&used_memory_mutex);
    return um;
}

/**
 * 内存分配线程安全开关
 */
void zmalloc_enable_thread_safeness(void) {
    zmalloc_thread_safe = 1;
}
