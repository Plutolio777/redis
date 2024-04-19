/* adlist.h - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

/**
 * 定义了链表的节点 可以看到这里使用的是一个双向链表
 */
typedef struct listNode {
    // 前向指针  指向上一个节点
    struct listNode *prev;
    // 后向指针 指向下一个节点
    struct listNode *next;
    // 节点保存的值 C语言中 void*可以指向任意类型
    void *value;
    // AL_START_HEAD 0 从头遍历
    // AL_START_TAIL 1 从尾遍历
} listNode;

/*
 * 用于迭代链表的迭代器
 */
typedef struct listIter {
    // 遍历的当前节点对象
    listNode *next;
    // 这个是一个标志位 表示向前遍历还是向后遍历
    int direction;
} listIter;

/**
 * 双向链表结构体 拥有链表的头尾指针
 */
typedef struct list {
    listNode *head;
    listNode *tail;
    // 定义的实例方法
    // 用于链表的复制
    void *(*dup)(void *ptr);
    // 用于链表内存释放
    void (*free)(void *ptr);
    // 用于链表的匹配
    int (*match)(void *ptr, void *key);
    // 定义链表的长度
    unsigned int len;
} list;

/* Functions implemented as macros */
// 宏定义
#define listLength(l) ((l)->len) // 获取链表长度
#define listFirst(l) ((l)->head) // 获取表头
#define listLast(l) ((l)->tail) // 获取表尾
#define listPrevNode(n) ((n)->prev) // 获取前置节点
#define listNextNode(n) ((n)->next) // 获取后置节点
#define listNodeValue(n) ((n)->value) // 获取节点值

#define listSetDupMethod(l,m) ((l)->dup = (m)) // 设置复制方法
#define listSetFreeMethod(l,m) ((l)->free = (m)) // 设置释放内存的方法
#define listSetMatchMethod(l,m) ((l)->match = (m)) // 设置匹配的方法

#define listGetDupMethod(l) ((l)->dup) // 获取复制方法
#define listGetFree(l) ((l)->free) // 获取释放内存的方法
#define listGetMatchMethod(l) ((l)->match) // 获取匹配值的方法

/* Prototypes */

list *listCreate(void);
void listRelease(list *list);
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
void listDelNode(list *list, listNode *node);
listIter *listGetIterator(list *list, int direction);
listNode *listNext(listIter *iter);
void listReleaseIterator(listIter *iter);
list *listDup(list *orig);
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, int index);
void listRewind(list *list, listIter *li);
void listRewindTail(list *list, listIter *li);

// 定义了迭代器迭代遍历的方向
/* Directions for iterators */
#define AL_START_HEAD 0
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
