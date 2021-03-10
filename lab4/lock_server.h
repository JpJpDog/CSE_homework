// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include "lock_client.h"
#include "lock_protocol.h"
#include "rpc.h"
#include <pthread.h>
#include <string>

struct Lock {
    bool is_free;
    int clt;
    pthread_cond_t cond;
    Lock(bool f, int c)
        : is_free(f)
        , clt(c)
    {
        pthread_cond_init(&cond, NULL);
    }
    ~Lock()
    {
        pthread_cond_destroy(&cond);
    }
};

class lock_server {

protected:
    int nacquire;
    pthread_mutex_t mutex;
    std::map<lock_protocol::lockid_t, Lock> lock_map;

public:
    lock_server();
    ~lock_server();
    lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int&);
    lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int&);
    lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int&);
};

#endif
