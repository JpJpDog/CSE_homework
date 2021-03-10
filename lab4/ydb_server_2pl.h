#ifndef ydb_server_2pl_h
#define ydb_server_2pl_h

#include <map>
#include <string>
#include <vector>
#include "extent_client.h"
#include "lock_client.h"
#include "lock_client_cache.h"
#include "lock_protocol.h"
#include "ydb_protocol.h"
#include "ydb_server.h"

class ydb_server_2pl : public ydb_server
{
private:
    enum TValueStatus {
        READ,
        WRITE,
    };
    struct TValue {
        std::string value;
        TValueStatus status;
        TValue(TValueStatus s, const std::string& str)
            : status(s)
            , value(str)
        {
        }
    };
    typedef std::map<extent_protocol::extentid_t, TValue> TContent;
    typedef std::pair<extent_protocol::extentid_t, TValue> TVPair;
    typedef TContent::iterator TVIter;
    typedef std::map<ydb_protocol::transaction_id, TContent> TMap;
    typedef std::pair<ydb_protocol::transaction_id, TContent> TPair;
    typedef TMap::iterator TIter;
    struct TIters {
        TIter t;
        TIters* next;
        TIters(TIter t, TIters* n)
            : t(t)
            , next(n)
        {
        }
    };
    struct LockInfo {
        TIter owner;
        TIters* followers;
        LockInfo()
            : owner(NULL)
            , followers(NULL)
        {
        }
    };

    ydb_protocol::transaction_id nextTransactionId;
    TMap tMap;
    LockInfo lockInfos[maxKey];
    lock_protocol::lockid_t tMapLockId;
    lock_protocol::lockid_t lockInfosLockId;

    void addFollower(lock_protocol::lockid_t lid, TIter t);
    void deleteFollower(lock_protocol::lockid_t lid, TIter t);
    void finishT(TIter iter, bool hasLock);
    bool hasPath(TIter start, TIter end);
    int checkAcquire(lock_protocol::lockid_t lid, TIter iter);
    ydb_protocol::status write(ydb_protocol::transaction_id, extent_protocol::extentid_t, const std::string&);


public:
    ydb_server_2pl(std::string, std::string);
    ~ydb_server_2pl();
    ydb_protocol::status transaction_begin(int, ydb_protocol::transaction_id&);
    ydb_protocol::status transaction_commit(ydb_protocol::transaction_id, int&);
    ydb_protocol::status transaction_abort(ydb_protocol::transaction_id, int&);
    ydb_protocol::status get(ydb_protocol::transaction_id, const std::string, std::string&);
    ydb_protocol::status set(ydb_protocol::transaction_id, const std::string, const std::string, int&);
    ydb_protocol::status del(ydb_protocol::transaction_id, const std::string, int&);
};

#endif
