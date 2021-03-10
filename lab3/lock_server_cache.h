#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"

#include <deque>

enum LockState
{
  FREE,
  USED,
  TOREVOKE
};

struct LockInfo
{
  LockState state;
  std::string acquiredId;
  std::deque<std::string> waitingIds;
  LockInfo() : state(FREE) {}
};

typedef std::map<lock_protocol::lockid_t, LockInfo> LockMap;
typedef LockMap::iterator MapIter;
typedef LockMap::value_type MapValue;

class lock_server_cache
{
private:
  int nacquire;
  LockMap lockMap;
  pthread_mutex_t lockMapMutex;

public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
