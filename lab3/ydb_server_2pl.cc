#include "ydb_server_2pl.h"
#include "extent_client.h"

void ydb_server_2pl::addFollower(lock_protocol::lockid_t lid, TIter t)
{
	// printf("$$L %d add follower %d$$\n", lid, t->first);
	this->lockInfos[lid].followers = new TIters(t, this->lockInfos[lid].followers);
}

void ydb_server_2pl::deleteFollower(lock_protocol::lockid_t lid, TIter t)
{
	// printf("$$L %d delete follower %d$$\n", lid, t->first);
	TIters *p = this->lockInfos[lid].followers, *q = NULL;
	while (p)
	{
		if (p->t->first == t->first)
		{
			if (q)
				q->next = p->next;
			else
				this->lockInfos[lid].followers = p->next;
			free(p);
			return;
		}
		q = p;
		p = p->next;
	}
	assert(0);
}

bool ydb_server_2pl::hasPath(TIter start, TIter end)
{
	// printf("$$check$$ T%d to T%d\n", start->first, end->first);
	if (start->first == end->first)
		return true;
	for (TVIter it = start->second.begin(); it != start->second.end(); it++)
	{
		// printf("$$T %d has lock %d$$\n", start->first, it->first);
		lock_protocol::lockid_t lid = (lock_protocol::lockid_t)it->first;
		for (TIters *startFollowers = this->lockInfos[lid].followers; startFollowers; startFollowers = startFollowers->next)
		{
			// printf("$$L %d has follower %d$$\n", lid, startFollowers->t->first);
			if (this->hasPath(startFollowers->t, end))
				return true;
		}
	}
	return false;
}

void ydb_server_2pl::finishT(TIter iter, bool hasLock = false)
{
	TContent *t = &(iter->second);
	for (TVIter it = t->begin(); it != t->end(); it++)
	{
		if (!hasLock || it != t->begin())
			this->lc->acquire(this->lockInfosLockId);
		lock_protocol::lockid_t lid = (lock_protocol::lockid_t)it->first;
		this->lockInfos[lid].owner = this->tMap.end();
		if (this->lockInfos[lid].followers == NULL)
			this->lc->release(this->lockInfosLockId);
		this->lc->release(lid);
	}
	this->lc->acquire(this->tMapLockId);
	this->tMap.erase(iter);
	this->lc->release(this->tMapLockId);
}

int ydb_server_2pl::checkAcquire(lock_protocol::lockid_t lid, TIter waiter)
{
	this->lc->acquire(this->lockInfosLockId);
	TIter owner = this->lockInfos[lid].owner;
	if (owner != this->tMap.end())
	{
		if (this->hasPath(waiter, owner))
		{
			printf("##abort for dead lock## %d\n", waiter->first);
			this->finishT(waiter, true);
			return 1;
		}
		this->addFollower(lid, waiter);
		this->lc->release(this->lockInfosLockId);
		this->lc->acquire(lid);
		this->lockInfos[lid].owner = waiter;
		this->deleteFollower(lid, waiter);
		this->lc->release(this->lockInfosLockId);
	}
	else
	{
		this->lockInfos[lid].owner = waiter;
		this->lc->release(this->lockInfosLockId);
		this->lc->acquire(lid);
	}
	return 0;
}

ydb_server_2pl::ydb_server_2pl(std::string extent_dst, std::string lock_dst)
	: ydb_server(extent_dst, lock_dst)
{
	this->tMapLockId = this->maxKey;
	this->lockInfosLockId = this->maxKey + 1;
	this->nextTransactionId = 1;
	for (int i = 0; i < maxKey; i++)
		this->lockInfos[i].owner = this->tMap.end();
}

ydb_server_2pl::~ydb_server_2pl() {}

ydb_protocol::status ydb_server_2pl::transaction_begin(int, ydb_protocol::transaction_id &out_id)
{
	this->lc->acquire(this->tMapLockId);
	out_id = this->nextTransactionId++;
	this->tMap.insert(TPair(out_id, {}));
	this->lc->release(this->tMapLockId);
	printf("##begin## %d\n", out_id);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_2pl::transaction_commit(ydb_protocol::transaction_id id, int &)
{
	printf("##commit## %d\n", id);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	for (TVIter it = iter->second.begin(); it != iter->second.end(); it++)
	{
		if (it->second.status == WRITE)
		{
			if (it->second.value.size() != 0)
				this->ec->put(it->first, it->second.value);
			else
				this->ec->remove(it->first);
		}
	}
	this->finishT(iter);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_2pl::transaction_abort(ydb_protocol::transaction_id id, int &)
{
	printf("##abort## %d\n", id);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	this->finishT(iter);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_2pl::get(ydb_protocol::transaction_id id, const std::string key, std::string &out_value)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##get## tid: %d, key: %d\n", id, eid);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	TVIter it = iter->second.find(eid);
	if (it == iter->second.end())
	{
		if (this->checkAcquire((lock_protocol::lockid_t)eid, iter))
			return ydb_protocol::ABORT;
		this->ec->get(eid, out_value);
		this->lc->acquire(this->tMapLockId);
		iter->second.insert(TVPair(eid, TValue(READ, out_value)));
		this->lc->release(this->tMapLockId);
	}
	else
		out_value = it->second.value;
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_2pl::write(ydb_protocol::transaction_id id, extent_protocol::extentid_t eid, const std::string &value)
{
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	TVIter it = iter->second.find(eid);
	if (it == iter->second.end())
	{
		if (this->checkAcquire((lock_protocol::lockid_t)eid, iter))
			return ydb_protocol::ABORT;
		this->lc->acquire(this->tMapLockId);
		iter->second.insert(TVPair(eid, TValue(WRITE, value)));
		this->lc->release(this->tMapLockId);
	}
	else
	{
		it->second.status = WRITE;
		it->second.value = value;
	}
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_2pl::set(ydb_protocol::transaction_id id, const std::string key, const std::string value, int &)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##set## tid: %d, key: %d, value: %s\n", id, eid, value.c_str());
	return this->write(id, eid, value);
}

ydb_protocol::status ydb_server_2pl::del(ydb_protocol::transaction_id id, const std::string key, int &)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##delete## tid: %d, eid: %d\n", id, eid);
	return this->write(id, eid, "");
}
