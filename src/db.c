#include "redis.h"

#include <signal.h>

/*-----------------------------------------------------------------------------
 * C-level DB API
 *----------------------------------------------------------------------------*/

/**
 * @brief 在指定的数据库中查找给定键对应的值对象
 *
 * @param db  要查找的数据库指针
 * @param key 要查找的键对象
 *
 * @return 如果找到键，返回对应的值对象；否则返回NULL
 *
 * @details
 * 功能说明:
 * 1. 在数据库的字典中查找指定键
 * 2. 如果找到键，更新对象的访问时间用于LRU算法
 * 3. 处理虚拟内存相关的对象加载和状态管理
 * 4. 更新统计信息（命中/未命中计数）
 *
 * 执行流程：
 * - 使用 [dictFind](file://E:\MyFactory\redis\src\dict.c#L390-L409) 在数据库字典中查找键
 * - 如果找到键：
 *   - 更新LRU时钟（在没有后台保存进程时）
 *   - 处理虚拟内存状态：
 *     - 如果对象在内存中或正在交换出去，可能取消IO操作
 *     - 如果对象已交换到磁盘，则加载回内存
 *   - 增加命中统计并返回值对象
 * - 如果未找到键，增加未命中统计并返回NULL
 */
robj *lookupKey(redisDb *db, robj *key) {
    dictEntry *de = dictFind(db->dict,key->ptr);
    if (de) {
        robj *val = dictGetEntryVal(de);

        /* Update the access time for the aging algorithm.
         * Don't do it if we have a saving child, as this will trigger
         * a copy on write madness. */
        if (server.bgsavechildpid == -1 && server.bgrewritechildpid == -1)
            val->lru = server.lruclock;

        if (server.vm_enabled) {
            if (val->storage == REDIS_VM_MEMORY || val->storage == REDIS_VM_SWAPPING){
                /* If we were swapping the object out, cancel the operation */
                if (val->storage == REDIS_VM_SWAPPING)
                    vmCancelThreadedIOJob(val);
            } else {
                int notify = (val->storage == REDIS_VM_LOADING);

                /* Our value was swapped on disk. Bring it at home. */
                redisAssert(val->type == REDIS_VMPOINTER);
                val = vmLoadObject(val);
                dictGetEntryVal(de) = val;

                /* Clients blocked by the VM subsystem may be waiting for
                 * this key... */
                if (notify) handleClientsBlockedOnSwappedKey(db,key);
            }
        }
        server.stat_keyspace_hits++;
        return val;
    } else {
        server.stat_keyspace_misses++;
        return NULL;
    }
}


robj *lookupKeyRead(redisDb *db, robj *key) {
    expireIfNeeded(db,key);
    return lookupKey(db,key);
}

robj *lookupKeyWrite(redisDb *db, robj *key) {
    expireIfNeeded(db,key);
    return lookupKey(db,key);
}

robj *lookupKeyReadOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o = lookupKeyRead(c->db, key);
    if (!o) addReply(c,reply);
    return o;
}

robj *lookupKeyWriteOrReply(redisClient *c, robj *key, robj *reply) {
    robj *o = lookupKeyWrite(c->db, key);
    if (!o) addReply(c,reply);
    return o;
}

/* Add the key to the DB. If the key already exists REDIS_ERR is returned,
 * otherwise REDIS_OK is returned, and the caller should increment the
 * refcount of 'val'.
 *
 * @param db: 数据库指针
 * @param key: 要添加的键对象
 * @param val: 要添加的值对象
 * @return: 成功返回REDIS_OK，键已存在返回REDIS_ERR
 */
int dbAdd(redisDb *db, robj *key, robj *val) {
    // 查找对应的db中是否已经存在需要添加的key如果存在则添加失败
    if (dictFind(db->dict, key->ptr) != NULL) {
        return REDIS_ERR;
    } else {
        // 复制sds 然后添加到dict中
        sds copy = sdsdup(key->ptr);
        dictAdd(db->dict, copy, val);
        return REDIS_OK;
    }
}

/**
 * 在Redis数据库中替换或添加键值对
 * @param db Redis数据库指针
 * @param key 键对象指针
 * @param val 值对象指针
 * @return 如果是新添加的键值对返回1，如果是替换现有键值对返回0
 */
int dbReplace(redisDb *db, robj *key, robj *val) {
    /* 检查键是否已存在 */
    if (dictFind(db->dict,key->ptr) == NULL) {
        /* 键不存在，创建键的副本并添加新的键值对 */
        sds copy = sdsdup(key->ptr);
        dictAdd(db->dict, copy, val);
        return 1;
    } else {
        /* 键已存在，替换对应的值 */
        dictReplace(db->dict, key->ptr, val);
        return 0;
    }
}



int dbExists(redisDb *db, robj *key) {
    return dictFind(db->dict,key->ptr) != NULL;
}

/* Return a random key, in form of a Redis object.
 * If there are no keys, NULL is returned.
 *
 * The function makes sure to return keys not already expired. */
robj *dbRandomKey(redisDb *db) {
    struct dictEntry *de;

    while(1) {
        sds key;
        robj *keyobj;

        de = dictGetRandomKey(db->dict);
        if (de == NULL) return NULL;

        key = dictGetEntryKey(de);
        keyobj = createStringObject(key,sdslen(key));
        if (dictFind(db->expires,key)) {
            if (expireIfNeeded(db,keyobj)) {
                decrRefCount(keyobj);
                continue; /* search for another key. This expired. */
            }
        }
        return keyobj;
    }
}

/* Delete a key, value, and associated expiration entry if any, from the DB */
int dbDelete(redisDb *db, robj *key) {
    /* If VM is enabled make sure to awake waiting clients for this key:
     * deleting the key will kill the I/O thread bringing the key from swap
     * to memory, so the client will never be notified and unblocked if we
     * don't do it now. */
    if (server.vm_enabled) handleClientsBlockedOnSwappedKey(db,key);
    /* Deleting an entry from the expires dict will not free the sds of
     * the key, because it is shared with the main dictionary. */
    if (dictSize(db->expires) > 0) dictDelete(db->expires,key->ptr);
    return dictDelete(db->dict,key->ptr) == DICT_OK;
}

/* Empty the whole database */
long long emptyDb() {
    int j;
    long long removed = 0;

    for (j = 0; j < server.dbnum; j++) {
        removed += dictSize(server.db[j].dict);
        dictEmpty(server.db[j].dict);
        dictEmpty(server.db[j].expires);
    }
    return removed;
}


/**
 * 选择Redis数据库
 * @param c Redis客户端指针
 * @param id 数据库ID
 * @return 成功返回REDIS_OK，失败返回REDIS_ERR
 */
int selectDb(redisClient *c, int id) {
    /* 检查数据库ID是否有效 */
    if (id < 0 || id >= server.dbnum)
        return REDIS_ERR;
    /* 设置客户端当前数据库 */
    c->db = &server.db[id];
    return REDIS_OK;
}


/*-----------------------------------------------------------------------------
 * Type agnostic commands operating on the key space
 *----------------------------------------------------------------------------*/

void flushdbCommand(redisClient *c) {
    server.dirty += dictSize(c->db->dict);
    touchWatchedKeysOnFlush(c->db->id);
    dictEmpty(c->db->dict);
    dictEmpty(c->db->expires);
    addReply(c,shared.ok);
}

void flushallCommand(redisClient *c) {
    touchWatchedKeysOnFlush(-1);
    server.dirty += emptyDb();
    addReply(c,shared.ok);
    if (server.bgsavechildpid != -1) {
        kill(server.bgsavechildpid,SIGKILL);
        rdbRemoveTempFile(server.bgsavechildpid);
    }
    if (server.saveparamslen > 0) {
        /* Normally rdbSave() will reset dirty, but we don't want this here
         * as otherwise FLUSHALL will not be replicated nor put into the AOF. */
        int saved_dirty = server.dirty;
        rdbSave(server.dbfilename);
        server.dirty = saved_dirty;
    }
    server.dirty++;
}

void delCommand(redisClient *c) {
    int deleted = 0, j;

    for (j = 1; j < c->argc; j++) {
        if (dbDelete(c->db,c->argv[j])) {
            touchWatchedKey(c->db,c->argv[j]);
            server.dirty++;
            deleted++;
        }
    }
    addReplyLongLong(c,deleted);
}

void existsCommand(redisClient *c) {
    expireIfNeeded(c->db,c->argv[1]);
    if (dbExists(c->db,c->argv[1])) {
        addReply(c, shared.cone);
    } else {
        addReply(c, shared.czero);
    }
}

void selectCommand(redisClient *c) {
    int id = atoi(c->argv[1]->ptr);

    if (selectDb(c,id) == REDIS_ERR) {
        addReplyError(c,"invalid DB index");
    } else {
        addReply(c,shared.ok);
    }
}

void randomkeyCommand(redisClient *c) {
    robj *key;

    if ((key = dbRandomKey(c->db)) == NULL) {
        addReply(c,shared.nullbulk);
        return;
    }

    addReplyBulk(c,key);
    decrRefCount(key);
}

void keysCommand(redisClient *c) {
    dictIterator *di;
    dictEntry *de;
    sds pattern = c->argv[1]->ptr;
    int plen = sdslen(pattern), allkeys;
    unsigned long numkeys = 0;
    void *replylen = addDeferredMultiBulkLength(c);

    di = dictGetIterator(c->db->dict);
    allkeys = (pattern[0] == '*' && pattern[1] == '\0');
    while((de = dictNext(di)) != NULL) {
        sds key = dictGetEntryKey(de);
        robj *keyobj;

        if (allkeys || stringmatchlen(pattern,plen,key,sdslen(key),0)) {
            keyobj = createStringObject(key,sdslen(key));
            if (expireIfNeeded(c->db,keyobj) == 0) {
                addReplyBulk(c,keyobj);
                numkeys++;
            }
            decrRefCount(keyobj);
        }
    }
    dictReleaseIterator(di);
    setDeferredMultiBulkLength(c,replylen,numkeys);
}

void dbsizeCommand(redisClient *c) {
    addReplyLongLong(c,dictSize(c->db->dict));
}

void lastsaveCommand(redisClient *c) {
    addReplyLongLong(c,server.lastsave);
}

void typeCommand(redisClient *c) {
    robj *o;
    char *type;

    o = lookupKeyRead(c->db,c->argv[1]);
    if (o == NULL) {
        type = "none";
    } else {
        switch(o->type) {
        case REDIS_STRING: type = "string"; break;
        case REDIS_LIST: type = "list"; break;
        case REDIS_SET: type = "set"; break;
        case REDIS_ZSET: type = "zset"; break;
        case REDIS_HASH: type = "hash"; break;
        default: type = "unknown"; break;
        }
    }
    addReplyStatus(c,type);
}

void saveCommand(redisClient *c) {
    if (server.bgsavechildpid != -1) {
        addReplyError(c,"Background save already in progress");
        return;
    }
    if (rdbSave(server.dbfilename) == REDIS_OK) {
        addReply(c,shared.ok);
    } else {
        addReply(c,shared.err);
    }
}

void bgsaveCommand(redisClient *c) {
    if (server.bgsavechildpid != -1) {
        addReplyError(c,"Background save already in progress");
        return;
    }
    if (rdbSaveBackground(server.dbfilename) == REDIS_OK) {
        addReplyStatus(c,"Background saving started");
    } else {
        addReply(c,shared.err);
    }
}

void shutdownCommand(redisClient *c) {
    if (prepareForShutdown() == REDIS_OK)
        exit(0);
    addReplyError(c,"Errors trying to SHUTDOWN. Check logs.");
}

void renameGenericCommand(redisClient *c, int nx) {
    robj *o;

    /* To use the same key as src and dst is probably an error */
    if (sdscmp(c->argv[1]->ptr,c->argv[2]->ptr) == 0) {
        addReply(c,shared.sameobjecterr);
        return;
    }

    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.nokeyerr)) == NULL)
        return;

    incrRefCount(o);
    if (dbAdd(c->db,c->argv[2],o) == REDIS_ERR) {
        if (nx) {
            decrRefCount(o);
            addReply(c,shared.czero);
            return;
        }
        dbReplace(c->db,c->argv[2],o);
    }
    dbDelete(c->db,c->argv[1]);
    touchWatchedKey(c->db,c->argv[1]);
    touchWatchedKey(c->db,c->argv[2]);
    server.dirty++;
    addReply(c,nx ? shared.cone : shared.ok);
}

void renameCommand(redisClient *c) {
    renameGenericCommand(c,0);
}

void renamenxCommand(redisClient *c) {
    renameGenericCommand(c,1);
}

void moveCommand(redisClient *c) {
    robj *o;
    redisDb *src, *dst;
    int srcid;

    /* Obtain source and target DB pointers */
    src = c->db;
    srcid = c->db->id;
    if (selectDb(c,atoi(c->argv[2]->ptr)) == REDIS_ERR) {
        addReply(c,shared.outofrangeerr);
        return;
    }
    dst = c->db;
    selectDb(c,srcid); /* Back to the source DB */

    /* If the user is moving using as target the same
     * DB as the source DB it is probably an error. */
    if (src == dst) {
        addReply(c,shared.sameobjecterr);
        return;
    }

    /* Check if the element exists and get a reference */
    o = lookupKeyWrite(c->db,c->argv[1]);
    if (!o) {
        addReply(c,shared.czero);
        return;
    }

    /* Try to add the element to the target DB */
    if (dbAdd(dst,c->argv[1],o) == REDIS_ERR) {
        addReply(c,shared.czero);
        return;
    }
    incrRefCount(o);

    /* OK! key moved, free the entry in the source DB */
    dbDelete(src,c->argv[1]);
    server.dirty++;
    addReply(c,shared.cone);
}

/*-----------------------------------------------------------------------------
 * Expires API
 *----------------------------------------------------------------------------*/

int removeExpire(redisDb *db, robj *key) {
    /* An expire may only be removed if there is a corresponding entry in the
     * main dict. Otherwise, the key will never be freed. */
    redisAssert(dictFind(db->dict,key->ptr) != NULL);
    return dictDelete(db->expires,key->ptr) == DICT_OK;
}

void setExpire(redisDb *db, robj *key, time_t when) {
    dictEntry *de;

    /* Reuse the sds from the main dict in the expire dict */
    de = dictFind(db->dict,key->ptr);
    redisAssert(de != NULL);
    dictReplace(db->expires,dictGetEntryKey(de),(void*)when);
}

/* Return the expire time of the specified key, or -1 if no expire
 * is associated with this key (i.e. the key is non volatile) */
time_t getExpire(redisDb *db, robj *key) {
    dictEntry *de;

    /* No expire? return ASAP */
    if (dictSize(db->expires) == 0 || (de = dictFind(db->expires,key->ptr)) == NULL) return -1;

    /* The entry was found in the expire dict, this means it should also
     * be present in the main dict (safety check). */
    redisAssert(dictFind(db->dict,key->ptr) != NULL);
    return (time_t) dictGetEntryVal(de);
}

/* Propagate expires into slaves and the AOF file.
 * When a key expires in the master, a DEL operation for this key is sent
 * to all the slaves and the AOF file if enabled.
 *
 * This way the key expiry is centralized in one place, and since both
 * AOF and the master->slave link guarantee operation ordering, everything
 * will be consistent even if we allow write operations against expiring
 * keys. */
void propagateExpire(redisDb *db, robj *key) {
    robj *argv[2];

    argv[0] = createStringObject("DEL",3);
    argv[1] = key;
    incrRefCount(key);

    if (server.appendonly)
        feedAppendOnlyFile(server.delCommand,db->id,argv,2);
    if (listLength(server.slaves))
        replicationFeedSlaves(server.slaves,db->id,argv,2);

    decrRefCount(argv[0]);
    decrRefCount(argv[1]);
}

int expireIfNeeded(redisDb *db, robj *key) {
    time_t when = getExpire(db,key);

    if (when < 0) return 0; /* No expire for this key */

    /* Don't expire anything while loading. It will be done later. */
    if (server.loading) return 0;

    /* If we are running in the context of a slave, return ASAP:
     * the slave key expiration is controlled by the master that will
     * send us synthesized DEL operations for expired keys.
     *
     * Still we try to return the right information to the caller, 
     * that is, 0 if we think the key should be still valid, 1 if
     * we think the key is expired at this time. */
    if (server.masterhost != NULL) {
        return time(NULL) > when;
    }

    /* Return when this key has not expired */
    if (time(NULL) <= when) return 0;

    /* Delete the key */
    server.stat_expiredkeys++;
    propagateExpire(db,key);
    return dbDelete(db,key);
}

/*-----------------------------------------------------------------------------
 * Expires Commands
 *----------------------------------------------------------------------------*/


/**
 * 设置键的过期时间通用命令处理函数
 * @param c 客户端连接对象指针
 * @param key 要设置过期时间的键对象
 * @param param 过期时间参数对象
 * @param offset 时间偏移量
 *
 * 该函数处理EXPIRE、EXPIREAT等设置键过期时间的命令，根据参数设置键的过期时间，
 * 如果时间为负数或已过期则删除键，否则设置相应的过期时间。
 */
void expireGenericCommand(redisClient *c, robj *key, robj *param, long offset) {
    dictEntry *de;
    long seconds;

    // 获取过期时间参数值
    if (getLongFromObjectOrReply(c, param, &seconds, NULL) != REDIS_OK) return;

    // 根据偏移量调整过期时间
    seconds -= offset;

    // 查找键是否存在于数据库中
    de = dictFind(c->db->dict,key->ptr);
    if (de == NULL) {
        addReply(c,shared.czero);
        return;
    }

    /* 当TTL为负数或时间戳已过期时，在AOF加载或从实例上下文中不应直接执行DEL操作
     *
     * 而是采用IF语句的另一个分支来设置过期时间（可能已过期），
     * 并等待主节点发送明确的DEL命令。*/
    if (seconds <= 0 && !server.loading && !server.masterhost) {
        robj *aux;

        redisAssert(dbDelete(c->db,key));
        server.dirty++;

        /* 将此操作重写为显式的DEL命令进行复制/AOF持久化 */
        aux = createStringObject("DEL",3);
        rewriteClientCommandVector(c,2,aux,key);
        decrRefCount(aux);
        touchWatchedKey(c->db,key);
        addReply(c, shared.cone);
        return;
    } else {
        // 计算并设置过期时间
        time_t when = time(NULL)+seconds;
        setExpire(c->db,key,when);
        addReply(c,shared.cone);
        touchWatchedKey(c->db,key);
        server.dirty++;
        return;
    }
}


void expireCommand(redisClient *c) {
    expireGenericCommand(c,c->argv[1],c->argv[2],0);
}

void expireatCommand(redisClient *c) {
    expireGenericCommand(c,c->argv[1],c->argv[2],time(NULL));
}

void ttlCommand(redisClient *c) {
    time_t expire, ttl = -1;

    expire = getExpire(c->db,c->argv[1]);
    if (expire != -1) {
        ttl = (expire-time(NULL));
        if (ttl < 0) ttl = -1;
    }
    addReplyLongLong(c,(long long)ttl);
}

void persistCommand(redisClient *c) {
    dictEntry *de;

    de = dictFind(c->db->dict,c->argv[1]->ptr);
    if (de == NULL) {
        addReply(c,shared.czero);
    } else {
        if (removeExpire(c->db,c->argv[1])) {
            addReply(c,shared.cone);
            server.dirty++;
        } else {
            addReply(c,shared.czero);
        }
    }
}
