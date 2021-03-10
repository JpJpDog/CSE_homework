// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef std::map<lock_protocol::lockid_t, Lock>::iterator LockMapIter;
typedef std::map<lock_protocol::lockid_t, Lock>::value_type LockMapType;

lock_server::lock_server() : nacquire(0)
{
	pthread_mutex_init(&(this->mutex), NULL);
}

lock_server::~lock_server()
{
	pthread_mutex_destroy(&(this->mutex));
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	printf("stat request from clt %d\n", clt);
	r = nacquire;
	return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&(this->mutex));
	LockMapIter it = this->lock_map.find(lid);
	if (it == this->lock_map.end())
	{
		this->lock_map.insert(LockMapType(lid, Lock(false, clt)));
		pthread_mutex_unlock(&(this->mutex));
	}
	else if (it->second.is_free)
	{
		it->second.clt = clt;
		it->second.is_free = false;
		pthread_mutex_unlock(&(this->mutex));
	}
	else
	{
		pthread_mutex_lock(&(it->second.mutex));
		pthread_mutex_unlock(&(this->mutex));
		while (!it->second.is_free)
			pthread_cond_wait(&(it->second.cond), &(it->second.mutex));
		it->second.clt = clt;
		it->second.is_free = false;
		pthread_mutex_unlock(&(it->second.mutex));
	}
	return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	LockMapIter it = this->lock_map.find(lid);
	if (it == this->lock_map.end() || it->second.clt != clt || it->second.is_free)
		return lock_protocol::RPCERR;
	pthread_mutex_lock(&(it->second.mutex));
	it->second.is_free = true;
	pthread_cond_signal(&(it->second.cond));
	pthread_mutex_unlock(&(it->second.mutex));
	return ret;
}
