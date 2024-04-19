/* adlist.c - A generic doubly linked list implementation
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/***
 * 创建一个新列表。创建的列表可以使用 AlFreeList（） 释放，但用户需要在调用 AlFreeList（） 之前释放每个节点的私有值。出错时，返回 NULL。否则为指向新列表的指针。
 * @return 返回新创建的链表
 */
list *listCreate(void)
{
    struct list *list;
    // 调用zmalloc分配内存空间
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    // 赋完初始值就返回了 就是这么简单 一个双向链表就创建好了

    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/*
 * 释放一整个链表
 * Free the whole list.
 *
 * This function can't fail.
 * @param list
 * */
void listRelease(list *list)
{
    unsigned int len;
    listNode *current, *next;

    current = list->head;
    len = list->len;
    while(len--) {
        // 在current释放内存前先获取 下一个节点的地址
        next = current->next;
        // 释放节点中的私有值
        if (list->free) list->free(current->value);
        // 释放节点
        zfree(current);

        current = next;
    }
    zfree(list);
}

/* Add a new node to the list, to head, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    // 先分配node节点内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    // 先把值存起来
    node->value = value;
    // 第一个节点 头尾指针指向该节点 前置后置节点赋空即可
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        // 添加到链表头部
        // 当前节点的前置节点为NULL
        node->prev = NULL;
        //           node
        //             |
        //             V
        // head ---> node node node node <--tail
        node->next = list->head;
        //           node
        //            A |
        //            | V
        // head --->  node  node node node <--tail
        list->head->prev = node;
        // head ---> node
        //            A |
        //            | V
        //             node  node node node <--tail
        list->head = node;
    }
    // 链表长度+1
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, contaning the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        // 从尾部添加节点与头部类似反向思维即可
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
        //        ____________
        //       |            |
        // node node<--(node)-->node
        // 将上一个节点的next指针从自己转而指向自己的下一个指针
        node->prev->next = node->next;
    else
        // 没有前置节点的话说明当前节点是第一个 头指针指向自己的下一个即可
        //        head
        //         V
        // (node) node node
        list->head = node->next;
    if (node->next)
        //       |--------------|
        //       |              V
        // node node<--(node)-->node
        //       A              |
        //       |______________|
        // 将上一个节点的next指针从自己转而指向自己的下一个指针
        node->next->prev = node->prev;
    else
        // 后置节点为空则说明是最后一个节点 尾指针指向自己的上一个节点即可
        list->tail = node->prev;
    // 清空节点value内存 这里节点的 pre 和next都还是指向原来的节点 应该实在这个里面去清除
    if (list->free) list->free(node->value);
    // 清空节点内存
    zfree(node);
    list->len--;
}

/*
 * 返回一个list的迭代器 @listIter
 * Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;
    // 分配迭代器内存
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    // 根据 direction 来选择是从头还是从尾部遍历
    // 从头就是把list的head作为迭代器的当前节点就可以
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

/*
 * 释放迭代器的内存 不需要释放next的内存哈
 * Release the iterator memory */
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/*
 * 相当于把迭代器重置成从头遍历
 * Create an iterator in the list private iterator structure */
void listRewind(list *list, listIter *li) {
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

/**
 * 相当于把迭代器重置成从尾遍历
 * @param list
 * @param li
 */
void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/*
 * 迭代器的迭代方法获取下一个迭代的节点
 * Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetItarotr(list,<direction>);
 * while ((node = listNextIterator(iter)) != NULL) {
 *     DoSomethingWith(listNodeValue(node));
 * }
 *
 * */
listNode *listNext(listIter *iter)
{
    listNode *current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }
    return current;
}

/*
 *
 * 链表的复制
 * Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
list *listDup(list *orig)
{
    list *copy;
    listIter *iter;
    listNode *node;
    // 分配需要copy的list内存
    if ((copy = listCreate()) == NULL)
        return NULL;
    // 然后先把这三个函数copy一下
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    // 获取迭代器
    iter = listGetIterator(orig, AL_START_HEAD);
    while((node = listNext(iter)) != NULL) {
        void *value;
        // 指定了dup方法的话用dup复制 否则直接获取值
        // dup方法调用如果为NULL的话复制list失败
        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                listReleaseIterator(iter);
                return NULL;
            }
        } else
            value = node->value;
        // 从头遍历就得从头插入
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            listReleaseIterator(iter);
            return NULL;
        }
    }
    // 释放迭代器内存
    listReleaseIterator(iter);
    return copy;
}

/*
 * 链表中查找对应的value值
 * Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
listNode *listSearchKey(list *list, void *key)
{
    listIter *iter;
    listNode *node;
    // 创建迭代器
    iter = listGetIterator(list, AL_START_HEAD);

    // 开始迭代
    while((node = listNext(iter)) != NULL) {
        // 如果指定了match方法的话就使用方法进行比对
        if (list->match) {
            if (list->match(node->value, key)) {
                listReleaseIterator(iter);
                return node;
            }
        // 否知直接比较两个值是否相等
        } else {
            if (key == node->value) {
                listReleaseIterator(iter);
                return node;
            }
        }
    }
    listReleaseIterator(iter);
    return NULL;
}

/*
 * 根据index位置获得节点
 * Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimante
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, int index) {
    listNode *n;
    // 支持-1 -1的话就是 0并且从尾节点遍历
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}
