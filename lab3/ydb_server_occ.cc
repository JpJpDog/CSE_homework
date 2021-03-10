#include "ydb_server_occ.h"
#include "extent_client.h"

int ydb_server_occ::getGeneration(const std::string &str)
{
	return (str.size() == 0) ? 0 : *(int *)str.c_str();
}

void ydb_server_occ::composeValue(int g, const std::string &v, std::string &str)
{
	std::string gStr;
	gStr.assign((char *)&g, sizeof(int));
	str = gStr + v;
}

void ydb_server_occ::decomposeValue(const std::string &str, int &g, std::string &v)
{
	v = (str.size() == 0) ? "" : str.substr(sizeof(int));
	g = getGeneration(str);
}

ydb_server_occ::ydb_server_occ(std::string extent_dst, std::string lock_dst) : ydb_server(extent_dst, lock_dst)
{
	this->tMapLockId = this->maxKey;
	this->commitLockId = this->maxKey + 1;
}

ydb_server_occ::~ydb_server_occ()
{
}

ydb_protocol::status ydb_server_occ::transaction_begin(int, ydb_protocol::transaction_id &out_id)
{
	this->lc->acquire(this->tMapLockId);
	out_id = this->nextTransactionId++;
	this->tMap.insert(TPair(out_id, {}));
	this->lc->release(this->tMapLockId);
	printf("##begin## tid: %d\n", out_id);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_occ::transaction_commit(ydb_protocol::transaction_id id, int &)
{
	ydb_protocol::status ret = ydb_protocol::OK;
	printf("##commit## tid: %d\n", id);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	this->lc->acquire(this->commitLockId);
	std::string buf;
	for (VIter it = iter->second.begin(); it != iter->second.end(); it++)
	{
		if (!it->second.readen)
			continue;
		this->ec->get(it->first, buf);
		int curGeneration = this->getGeneration(buf);
		if (curGeneration > it->second.generation)
		{
			printf("!!abort!! tid: %d\n", id);
			ret = ydb_protocol::ABORT;
			goto RET;
		}
	}
	for (VIter it = iter->second.begin(); it != iter->second.end(); it++)
	{
		if (!it->second.written)
			continue;
		this->lc->acquire((lock_protocol::lockid_t)it->first);
		if (it->second.value.size() == 0)
			this->ec->remove(it->first);
		else
		{
			this->composeValue(it->second.generation + 1, it->second.value, buf);
			this->ec->put(it->first, buf);
		}
		this->lc->release((lock_protocol::lockid_t)it->first);
	}
RET:
	this->lc->release(this->commitLockId);
	this->lc->acquire(this->tMapLockId);
	this->tMap.erase(iter);
	this->lc->release(this->tMapLockId);
	return ret;
}

ydb_protocol::status ydb_server_occ::transaction_abort(ydb_protocol::transaction_id id, int &)
{
	ydb_protocol::status ret = ydb_protocol::OK;
	printf("##abort## tid: %d\n", id);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	if (iter == this->tMap.end())
		ret = ydb_protocol::TRANSIDINV;
	else
		this->tMap.erase(iter);
	this->lc->release(this->tMapLockId);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_occ::get(ydb_protocol::transaction_id id, const std::string key, std::string &out_value)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##get## tid: %d, key: %d\n", id, eid);
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	VIter it = iter->second.find(eid);
	if (it == iter->second.end())
	{
		std::string buf;
		this->lc->acquire((lock_protocol::lockid_t)eid);
		this->ec->get(eid, buf);
		this->lc->release((lock_protocol::lockid_t)eid);
		int g;
		this->decomposeValue(buf, g, out_value);
		this->lc->acquire(this->tMapLockId);
		iter->second.insert(VPair(eid, TValue(g, true, false, out_value)));
		this->lc->release(this->tMapLockId);
	}
	else
	{
		it->second.readen = true;
		out_value = it->second.value;
	}
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_occ::write(ydb_protocol::transaction_id id, extent_protocol::extentid_t eid, const std::string &value)
{
	this->lc->acquire(this->tMapLockId);
	TIter iter = this->tMap.find(id);
	this->lc->release(this->tMapLockId);
	if (iter == this->tMap.end())
		return ydb_protocol::TRANSIDINV;
	VIter it = iter->second.find(eid);
	if (it == iter->second.end())
	{
		std::string buf;
		this->lc->acquire((lock_protocol::lockid_t)eid);
		this->ec->get(eid, buf);
		this->lc->release((lock_protocol::lockid_t)eid);
		int originGen = this->getGeneration(buf);
		this->lc->acquire(this->tMapLockId);
		iter->second.insert(VPair(eid, TValue(originGen, false, true, value)));
		this->lc->release(this->tMapLockId);
	}
	else
	{
		it->second.written = true;
		it->second.value = value;
	}
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server_occ::set(ydb_protocol::transaction_id id, const std::string key, const std::string value, int &)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##set## tid: %d, key: %d, value: %d\n", id, eid, (int)*value.c_str());
	return this->write(id, eid, value);
}

ydb_protocol::status ydb_server_occ::del(ydb_protocol::transaction_id id, const std::string key, int &)
{
	extent_protocol::extentid_t eid = hash(key);
	printf("##delete## tid: %d, key: %d\n", id, eid);
	return this->write(id, eid, "");
}
