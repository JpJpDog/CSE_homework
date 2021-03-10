// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst,
									 class lock_release_user *_lu)
	: lock_client(xdst), lu(_lu)
{
	srand(time(NULL) ^ last_port);
	rlock_port = ((rand() % 32000) | (0x1 << 10));
	const char *hname;
	// VERIFY(gethostname(hname, 100) == 0);
	hname = "127.0.0.1";
	std::ostringstream host;
	host << hname << ":" << rlock_port;
	id = host.str();
	last_port = rlock_port;
	rpcs *rlsrpc = new rpcs(rlock_port);
	rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
	rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);
	this->lockMapMutex = PTHREAD_MUTEX_INITIALIZER;
}

// acquire 锁，如果本地正使用则排队，如果本地不存在则向server发 acquire 直到返回值为 ok
lock_protocol::status lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	int ret = lock_protocol::OK;
	pthread_mutex_lock(&this->lockMapMutex);
	MapIter mapIter = this->lockMap.find(lid);
	if (mapIter == this->lockMap.end())
		mapIter = this->lockMap.insert(mapIter, MapType(lid, LockInfo()));
	LockInfo *info = &mapIter->second;
	tprintf("@@@ %s acquire lock %d, state:", this->id.c_str(), lid);
	switch (info->state)
	{
	case NONE:
		printf("none\n");
		info->state = ACQUIRING;
		break;
	case FREE:
		printf("free\n");
		info->state = USED;
		break;
	default: //
		printf("locked or acquiring or releasing\n");
		while (info->state != FREE && info->state != NONE)
			pthread_cond_wait(&info->lockCond, &this->lockMapMutex);
		if (info->state == NONE)
		{
			tprintf("@@@ %s acquire lock %d should acquire to server\n", this->id.c_str(), lid);
			info->state = ACQUIRING;
		}
		else //
		{
			tprintf("@@@ %s acquire lock %d acquire OK\n", this->id.c_str(), lid);
			info->state = USED;
		}
	}
	int something, rpcState;
	if (info->state != ACQUIRING)
		goto RET;
	while (true)
	{
		tprintf("@@@ %s acquire lock %d acquiring to server\n", this->id.c_str(), lid);
		pthread_mutex_unlock(&this->lockMapMutex);
		rpcState = cl->call(lock_protocol::acquire, lid, id, something);
		pthread_mutex_lock(&this->lockMapMutex);
		if (rpcState == lock_protocol::OK)
		{
			tprintf("@@@ %s acquire lock %d acquire to server OK\n", this->id.c_str(), lid);
			info->state = USED;
			break;
		}
		tprintf("@@@ %s acquire lock %d acquire to server RETRY\n", this->id.c_str(), lid);
		while (!info->toRetry)
			pthread_cond_wait(&info->retryCond, &this->lockMapMutex);
		info->toRetry = false;
	}
RET:
	pthread_mutex_unlock(&this->lockMapMutex);
	return ret;
}

// release 持有的锁， 如遇上revoke标记则revoke，通知等待的acquire继续（直接lock 或 重新acquire）
lock_protocol::status lock_client_cache::release(lock_protocol::lockid_t lid)
{
	int something;
	int ret = lock_protocol::OK;
	pthread_mutex_lock(&this->lockMapMutex);
	MapIter mapIter = this->lockMap.find(lid);
	LockInfo *info = NULL;
	if (mapIter == this->lockMap.end())
		goto RET;
	info = &mapIter->second;
	tprintf("@@@ %s release lock %d, ", this->id.c_str(), lid);
	if (info->toRevoke || info->state == RELEASING)
	{
		printf("revoke it\n");
		info->toRevoke = false;
		pthread_mutex_unlock(&this->lockMapMutex);
		cl->call(lock_protocol::release, lid, id, something);
		pthread_mutex_lock(&this->lockMapMutex);
		info->state = NONE;
	}
	else if (info->state == USED)
	{
		printf("free it\n");
		info->state = FREE;
	}
	pthread_cond_signal(&info->lockCond);
RET:
	pthread_mutex_unlock(&this->lockMapMutex);
	return ret;
}

// server 调用，让client 的lid锁被revoke或记上标记
rlock_protocol::status lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, int &)
{
	int something;
	int ret = rlock_protocol::OK;
	pthread_mutex_lock(&this->lockMapMutex);
	MapIter mapIter = this->lockMap.find(lid);
	LockInfo *info = NULL;
	if (mapIter == this->lockMap.end())
		goto RET;
	info = &mapIter->second;
	tprintf("@@@ %s handle revoke lock %d, state: ", this->id.c_str(), lid);
	switch (info->state)
	{
	case FREE:
		printf("free\n");
		info->state = RELEASING;
		pthread_mutex_unlock(&this->lockMapMutex);
		cl->call(lock_protocol::release, lid, id, something);
		pthread_mutex_lock(&this->lockMapMutex);
		info->state = NONE;
		pthread_cond_signal(&info->lockCond);
		break;
	case USED:
		printf("locked\n");
		info->state = RELEASING;
		break;
	default:
		printf("none or acquiring or releasing\n");
		info->toRevoke = true;
		break;
	}
RET:
	pthread_mutex_unlock(&this->lockMapMutex);
	return ret;
}

// server 调用，让此client重新尝试acquire lid锁
rlock_protocol::status lock_client_cache::retry_handler(lock_protocol::lockid_t lid, int &)
{
	int ret = rlock_protocol::OK;
	pthread_mutex_lock(&this->lockMapMutex);
	MapIter mapIter = this->lockMap.find(lid);
	assert(mapIter != this->lockMap.end());
	LockInfo *info = &mapIter->second;
	tprintf("@@@ %s handle retry lock %d\n", this->id.c_str(), lid);
	info->toRetry = true;
	pthread_cond_signal(&info->retryCond);
	pthread_mutex_unlock(&this->lockMapMutex);
	return ret;
}
