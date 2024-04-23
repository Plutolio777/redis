/* Hash Tables Implementation.
 * redis中hashtable的实现
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
 * 1.实现了随机增删改查的hashtable
 * 2.具备自动扩容功能
 * 3.采用链表法解决hash冲突
 *
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

#ifndef __DICT_H
#define __DICT_H

#define DICT_OK 0
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
#define DICT_NOTUSED(V) ((void) V)

/**
 * hash中的entry
 * 存储key value
 * 以及下一个entry的指针(采用链式法 每一个table原始实际上是一个entry链表)
 */
typedef struct dictEntry {
    void *key;
    void *val;
    struct dictEntry *next;
} dictEntry;

/*
 * 这个struct表示hash的类型
 * 定义类五个函数
 * redis实现了三个类型分别是
 * @dictTypeHeapStringCopyKey 实现了共用key的dict
 * @dictTypeHeapStrings 不共用任何值
 * @dictTypeHeapStringCopyKeyValue 实现了共用key value的dict
 */
typedef struct dictType {
    // 用于hash值计算的函数
    unsigned int (*hashFunction)(const void *key);

    // 用于key的复制
    void *(*keyDup)(void *privdata, const void *key);
    // 用于value的复制
    void *(*valDup)(void *privdata, const void *obj);
    // key的比较
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    // 对key进行销毁
    void (*keyDestructor)(void *privdata, void *key);
    // 对value进行销毁
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/**
 * dict的结构体
 *
 */
typedef struct dict {
    // 第一个entry的指针 表示dictEntry数组指针
    dictEntry **table;
    // dict类型
    dictType *type;
    // dict的容量
    unsigned long size;
    // 随机数种子 用于计算hash值
    unsigned long sizemask;
    // dict使用容量
    unsigned long used;
    // 私有数据
    void *privdata;
} dict;

/**
 * dict迭代器
 *
 */
typedef struct dictIterator {
    // dict指针
    dict *ht;
    // 当前索引
    int index;
    // 当前entry 和其指向的下一个entry
    dictEntry *entry, *nextEntry;
} dictIterator;

/* This is the initial size of every hash table */
// hashtable的初始容量
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/
// 释放value的空间 调用dictType的valDestructor
#define dictFreeEntryVal(ht, entry) \
    if ((ht)->type->valDestructor) \
        (ht)->type->valDestructor((ht)->privdata, (entry)->val)
// 设置value到entry 如果有复制方法则用复制方法复制一个value赋值 否则直接赋值
#define dictSetHashVal(ht, entry, _val_) do { \
    if ((ht)->type->valDup) \
        entry->val = (ht)->type->valDup((ht)->privdata, _val_); \
    else \
        entry->val = (_val_); \
} while(0)

// 释放key的空间 调用dictType的keyDestructor
#define dictFreeEntryKey(ht, entry) \
    if ((ht)->type->keyDestructor) \
        (ht)->type->keyDestructor((ht)->privdata, (entry)->key)

// 设置key到entry 如果有复制方法则用复制方法复制一个key赋值 否则直接赋值
#define dictSetHashKey(ht, entry, _key_) do { \
    if ((ht)->type->keyDup) \
        entry->key = (ht)->type->keyDup((ht)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

// 比较两个key的值 调用dictType的keyCompare
#define dictCompareHashKeys(ht, key1, key2) \
    (((ht)->type->keyCompare) ? \
        (ht)->type->keyCompare((ht)->privdata, key1, key2) : \
        (key1) == (key2))

// 计算key的hash值 调用dictType的hashFunction
#define dictHashKey(ht, key) (ht)->type->hashFunction(key)

// 获取entry的key值
#define dictGetEntryKey(he) ((he)->key)
// 获取entry的value值
#define dictGetEntryVal(he) ((he)->val)
// 获取dict的容量
#define dictSlots(ht) ((ht)->size)
// 获取dict的当前使用大小
#define dictSize(ht) ((ht)->used)

/* API */
dict *dictCreate(dictType *type, void *privDataPtr);
int dictExpand(dict *ht, unsigned long size);
int dictAdd(dict *ht, void *key, void *val);
int dictReplace(dict *ht, void *key, void *val);
int dictDelete(dict *ht, const void *key);
int dictDeleteNoFree(dict *ht, const void *key);
void dictRelease(dict *ht);
dictEntry * dictFind(dict *ht, const void *key);
int dictResize(dict *ht);
dictIterator *dictGetIterator(dict *ht);
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *ht);
void dictPrintStats(dict *ht);
unsigned int dictGenHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *ht);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
