# redis-1.6.2源码学习

## 本地如何构建
基于cygwin64进行构建
## 学习进度

### redis-cli.c
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
- [x] struct sds
- [x] struct sdshdr
- [x] sdsOomAbort
- [x] sdsnewlen
- [x] sdsempty
- [x] sdsnew
- [x] sdslen
- [x] sdsdup
- [x] sdsfree
- [ ] sdsavail
- [ ] sdsupdatelen
- [ ] sdsMakeRoomFor
- [ ] sdscatlen
- [ ] sdscat
- [ ] sdscpylen
- [ ] sdscpy
- [ ] sdstrim
- [ ] sdsrange
- [ ] sdstolower
- [ ] sdscmp
- [ ] sdssplitlen
