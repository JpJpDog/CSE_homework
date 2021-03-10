// the caching lock server implementation

#include "lock_server_cache.h"
#include "handle.h"
#include "lang/verify.h"
#include "tprintf.h"
#include <arpa/inet.h>
#include <sstream>
#include <stdio.h>
#include <unistd.h>

lock_server_cache::lock_server_cache()
    : lockMapMutex(PTHREAD_MUTEX_INITIALIZER)
{
}

int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, int&)
{
    int something;
    lock_protocol::status ret = lock_protocol::OK;
    pthread_mutex_lock(&this->lockMapMutex);
    MapIter mapIter = this->lockMap.find(lid);
    if (mapIter == this->lockMap.end())
        mapIter = this->lockMap.insert(mapIter, MapValue(lid, LockInfo()));
    LockInfo* info = &mapIter->second;
    bool toRevoke = false;
    // tprintf("@@@ %s acquire lock %d, state:", id.c_str(), lid);
    switch (info->state) {
    case FREE:
        // printf("free\n");
        info->state = USED;
        info->acquiredId = id;
        break;
    case USED:
        // printf("locked\n");
        info->state = TOREVOKE;
        info->waitingIds.push_back(id);
        ret = lock_protocol::RETRY;
        toRevoke = true;
        break;
    case TOREVOKE:
        // printf("toRevoke, ");
        if (*info->waitingIds.begin() == id) {
            printf("acquire now, ");
            info->acquiredId = id;
            info->waitingIds.pop_front();
            if (info->waitingIds.empty()) {
                // printf("no other wait\n");
                info->state = USED;
            } else {
                // printf("other wait\n");
                toRevoke = true;
            }
        } else {
            // printf("insert to queue\n");
            info->waitingIds.push_back(id);
            ret = lock_protocol::RETRY;
        }
    }
    if (toRevoke) {
        // tprintf("@@@ %s acquire lock %d, call revoke\n", id.c_str(), lid);
        handle h(info->acquiredId);
        rpcc* cl = h.safebind();
        assert(cl != NULL);
        pthread_mutex_unlock(&this->lockMapMutex);
        cl->call(rlock_protocol::revoke, lid, something);
        goto RET1;
    }
    pthread_mutex_unlock(&this->lockMapMutex);
RET1:
    return ret;
}

int lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int& r)
{
    int something;
    lock_protocol::status ret = lock_protocol::OK;
    pthread_mutex_lock(&this->lockMapMutex);
    MapIter mapIter = this->lockMap.find(lid);
    LockInfo* info = NULL;
    if (mapIter == this->lockMap.end())
        goto RET;
    info = &mapIter->second;
    // tprintf("@@@ %s release lock %d, state: ", id.c_str(), lid);
    if (info->state == FREE)
        goto RET;
    info->acquiredId = "";
    if (info->state == USED) {
        // printf("free\n");
        info->state = FREE;
    } else {
        // printf("to revoke\n");
        handle h(*info->waitingIds.begin());
        rpcc* cl = h.safebind();
        assert(cl != NULL);
        pthread_mutex_unlock(&this->lockMapMutex);
        cl->call(rlock_protocol::retry, lid, something);
        goto RET1;
    }
RET:
    pthread_mutex_unlock(&this->lockMapMutex);
RET1:
    return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int& r)
{
    // tprintf("stat request\n");
    r = nacquire;
    return lock_protocol::OK;
}
