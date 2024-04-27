# redis-1.6.2源码学习

## 本地如何构建
基于cygwin64进行构建
## 学习进度

### [redis-cli.c](redis-cli.c)
redis-cli.c 实现了 Redis 命令行客户端的核心功能，提供了丰富的命令和选项，使用户能够轻松地连接到 Redis 服务器，执行命令，并查看返回的结果。
- [x] struct redisCommand
- [x] lookupCommand
- [x] cliReadLine
- [x] cliReadSingleLineReply
- [x] cliReadBulkReply
- [ ] cliReadMultiBulkReply
- [x] cliReadReply
- [x] selectDb
- [x] cliSendCommand
- [x] parseOptions
- [ ] readArgFromStdin
- [x] usage
- [ ] convertToSds
- [x] prompt
- [x] strsep
- [x] repl
- [x] main
### sds.c
sds.c 文件包含了 SDS 数据结构的定义以及与其相关的一系列操作函数。SDS 被设计为兼容 C 语言的字符串，同时提供了更高级的功能，如自动的内存管理和长度计算。
- [x] struct sds
- [x] struct sdshdr
- [x] sdsOomAbort
- [x] sdsnewlen
- [x] sdsempty
- [x] sdsnew
- [x] sdslen
- [x] sdsdup
- [x] sdsfree
- [x] sdsavail
- [x] sdsupdatelen
- [x] sdsMakeRoomFor
- [x] sdscatlen
- [x] sdscat
- [x] sdscpylen
- [x] sdscpy
- [x] sdstrim
- [x] sdsrange
- [x] sdstolower
- [x] sdscmp
- [x] sdssplitlen

### [zmalloc.c](zmalloc.c)
zmalloc.c 是 Redis 内存管理模块的核心文件之一。在 Redis 中，内存管理是非常重要的，因为 Redis 是一个内存存储系统，所有的数据都存储在内存中。zmalloc.c 提供了一套定制的内存分配和释放函数，这些函数在 Redis 中被广泛使用，以确保内存管理的效率和安全。
- [x] increment_used_memory
- [x] decrement_used_memory
- [x] zmalloc
- [x] zrealloc
- [x] zfree
- [x] zstrdup
- [x] zmalloc_used_memory
- [x] zmalloc_enable_thread_safeness

### [adlist.c](adlist.c)

Redis中的`adlist.c`文件是实现了双端链表（double-ended list）数据结构的源代码。这个链表在Redis中被广泛使用，不仅用于构建诸如列表（List）这样的基础数据类型，还作为许多其他内部数据结构的构建块。

`adlist.c`实现的双端链表提供了丰富的操作接口，如节点插入、删除、查找等，使得Redis能够高效地进行链表操作。这些操作在Redis处理各种命令和内部逻辑时非常重要，因为它们提供了灵活且高效的数据存储和访问机制。

具体来说，Redis的`adlist.c`实现的双端链表有以下特点：

1. 动态内存管理：链表节点可以根据需要动态地创建和销毁，从而避免了固定大小数组的内存浪费问题。

2. 双向遍历：链表可以从头部到尾部遍历，也可以从尾部到头部遍历，这使得在链表两端进行插入和删除操作都非常高效。

3. 灵活性：链表可以存储任意类型的数据，通过指针的方式关联到实际的数据对象。这使得Redis能够方便地处理各种数据类型。

4. 可扩展性：链表结构本身可以很容易地进行扩展，比如添加新的操作函数或者优化现有算法，以适应Redis不断发展和变化的需求。

通过adlist.c实现的双端链表，Redis不仅提供了强大的列表数据类型支持，还为其他内部功能提供了坚实的基础。这种数据结构在Redis中的广泛应用，体现了Redis设计者在数据结构选择和实现上的深思熟虑和精湛技艺。

- [x] listCreate
- [x] listRelease
- [x] listAddNodeHead
- [x] listAddNodeTail
- [x] listDelNode
- [x] listGetIterator
- [x] listReleaseIterator
- [x] listRewind
- [x] listRewindTail
- [x] listNext
- [x] listDup
- [x] listSearchKey
- [x] listIndex

### [zipmap.c](zipmap.c)

Redis中的`zipmap.c`文件是实现了压缩映射（`Zipmap`）数据结构的源代码。压缩映射是Redis早期版本中使用的一种特殊数据结构，用于优化小对象存储的内存使用。

在Redis的早期实现中，当哈希表（`Hash`）或列表（`List`）等数据结构中的元素数量较少且较小时，Redis会采用压缩映射来存储这些元素。压缩映射通过压缩键和值的数据，减少了存储空间的占用，并提供了较为高效的访问性能。

zipmap.c中实现的压缩映射采用了连续的内存块来存储键值对，其中键和值都经过编码和压缩，以节省空间。这种紧凑的存储方式使得压缩映射在存储小对象时比传统的哈希表或列表更加高效。

然而，随着Redis的发展和用户需求的增长，压缩映射的局限性也逐渐显现出来。它对于大型数据集或包含大对象的场景并不适用，因为压缩和解压缩的开销可能会变得显著，并可能导致性能下降。因此，在Redis的后续版本中，压缩映射逐渐被更为高效和灵活的数据结构所替代。

尽管如此，zipmap.c的存在仍然具有一定的历史意义，它展示了Redis设计者在优化内存使用和性能方面的努力和创新。同时，对于了解Redis早期版本内部实现和演进的开发者来说，压缩映射也是一个值得研究的有趣话题。

- [x] zipmapNew
- [x] zipmapDecodeLength
- [x] zipmapEncodeLength
- [x] zipmapLookupRaw
- [x] zipmapRequiredLength
- [x] zipmapRawKeyLength
- [x] zipmapRawValueLength
- [x] zipmapRawEntryLength
- [x] zipmapRewind
- [x] zipmapNext
- [x] zipmapGet
- [x] zipmapExists
- [x] zipmapLen
- [x] zipmapRepr

### [dict.c](dict.c)

Redis中的dict.c文件是实现了字典（dictionary）数据结构的源代码，是Redis中非常重要的组成部分。字典是Redis用于存储键值对（key-value pair）的主要数据结构之一，提供了高效的键值查找、插入和删除操作。

dict.c实现的字典基于哈希表（hash table）来实现，通过计算键的哈希值来确定键在哈希表中的位置。这种设计使得Redis能够快速地根据键来查找、更新或删除对应的值。

字典的实现考虑了多种优化措施以提供高性能。首先，它支持动态扩容和缩容，当哈希表中元素数量超过一定阈值时，会自动进行扩容操作，以避免哈希冲突过多导致的性能下降；相反，当元素数量过少时，也会进行缩容操作以节省内存。

此外，dict.c还提供了丰富的操作接口，包括添加键值对、查找键对应的值、删除键值对等。这些操作都是基于哈希表的特性来设计的，能够在常数时间内完成，保证了Redis在处理大量键值对时的高效性。

字典在Redis中扮演着至关重要的角色。无论是Redis的基础数据类型（如字符串、哈希、列表等），还是Redis的高级功能（如事务、发布订阅等），都离不开字典的支持。因此，dict.c的实现质量和性能直接影响了Redis的整体性能和稳定性。

总的来说，dict.c是Redis中实现字典数据结构的关键文件，为Redis提供了高效、灵活的键值存储和访问机制，是Redis高性能和稳定性的重要保障之一。

- [x] _dictPanic
- [x] _dictAlloc
- [x] _dictFree
- [x] dictIntHashFunction
- [x] dictGenHashFunction
- [x] _dictCreate
- [x] _dictInit
- [x] dictResize
- [x] dictExpand
- [x] dictAdd
- [x] dictReplace
- [x] dictGenericDelete
- [x] dictDelete
- [x] dictDeleteNoFree
- [x] _dictClear
- [x] dictRelease
- [x] dictFind
- [x] dictGetIterator
- [x] dictNext
- [x] dictReleaseIterator
- [x] dictNext
- [x] dictGetRandomKey
- [x] _dictExpandIfNeeded
- [x] _dictNextPower
- [x] _dictKeyIndex
- [x] _dictEmpty
- [x] _dictPrintStats
- [x] _dictKeyIndex

### [anet.c](anet.c)

Redis中的anet.c文件主要实现了网络相关的功能，它是Redis网络通信的核心部分。该文件提供了一系列的网络编程接口，用于创建套接字（socket）、监听端口、接收和发送数据等，从而实现了Redis服务器与客户端之间的通信。

anet.c的核心功能包括：

1. 套接字创建与配置：anet.c提供了创建TCP和UDP套接字的功能，并根据需要进行配置，如设置套接字选项、绑定地址和端口等。

2. 监听与接受连接：Redis服务器需要监听特定的端口以接受客户端的连接请求。anet.c提供了监听端口和接受连接的功能，使得服务器能够等待并处理来自客户端的连接。

3. 数据发送与接收：一旦连接建立，anet.c负责数据的发送和接收。它提供了非阻塞和阻塞模式下的读写操作，使得Redis服务器能够高效地处理来自客户端的数据请求和响应。

4. 错误处理与日志记录：在网络通信过程中，可能会遇到各种错误情况，如连接断开、数据发送失败等。anet.c提供了相应的错误处理机制，并记录详细的日志信息，帮助开发者定位和解决问题。

通过anet.c实现的网络功能，Redis能够与其他应用程序进行高效的通信，实现了数据的共享和交互。无论是Redis服务器与客户端之间的通信，还是Redis集群节点之间的通信，都离不开anet.c的支持。

总之，anet.c是Redis中实现网络通信的关键文件，它提供了丰富的网络编程接口和错误处理机制，为Redis的通信功能提供了坚实的基础。

- [x] anetSetError
- [x] anetNonBlock
- [x] anetTcpNoDelay
- [x] anetSetSendBuffer
- [x] anetTcpKeepAlive
- [x] anetResolve
- [x] anetTcpGenericConnect
- [x] anetTcpConnect
- [x] anetTcpNonBlockConnect
- [x] anetRead
- [x] anetWrite
- [x] anetTcpServer
- [x] anetAccept


