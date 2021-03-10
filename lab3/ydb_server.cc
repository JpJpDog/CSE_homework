#include "ydb_server.h"
#include "extent_client.h"

//#define DEBUG 1

static long timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

ydb_server::ydb_server(std::string extent_dst, std::string lock_dst)
{
	ec = new extent_client(extent_dst);
	lc = new lock_client(lock_dst);
	//lc = new lock_client_cache(lock_dst);

	long starttime = timestamp();
	for (int i = 2; i < this->maxKey; i++)
	{ // for simplicity, just pre alloc all the needed inodes
		extent_protocol::extentid_t id;
		ec->create(extent_protocol::T_FILE, id);
	}

	long endtime = timestamp();
	printf("time %ld ms\n", endtime - starttime);
}

ydb_server::~ydb_server()
{
	delete lc;
	delete ec;
}

ydb_protocol::status ydb_server::transaction_begin(int, ydb_protocol::transaction_id &out_id)
{ // the first arg is not used, it is just a hack to the rpc lib
	// no imply, just return OK
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server::transaction_commit(ydb_protocol::transaction_id id, int &)
{
	// no imply, just return OK
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server::transaction_abort(ydb_protocol::transaction_id id, int &)
{
	// no imply, just return OK
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server::get(ydb_protocol::transaction_id id, const std::string key, std::string &out_value)
{
	// lab3: your code here
	extent_protocol::extentid_t eid = hash(key);
	printf("##get## key %s, eid %d\n", key.c_str(), out_value.c_str(), eid);
	lc->acquire((lock_protocol::lockid_t)eid);
	ec->get(eid, out_value);
	lc->release((lock_protocol::lockid_t)eid);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server::set(ydb_protocol::transaction_id id, const std::string key, const std::string value, int &)
{
	// lab3: your code here
	extent_protocol::extentid_t eid = hash(key);
	printf("##set## key %s, value %s, eid %d\n", key.c_str(), value.c_str(), eid);
	lc->acquire((lock_protocol::lockid_t)eid);
	ec->put(eid, value);
	lc->release((lock_protocol::lockid_t)eid);
	return ydb_protocol::OK;
}

ydb_protocol::status ydb_server::del(ydb_protocol::transaction_id id, const std::string key, int &)
{
	// lab3: your code here
	extent_protocol::extentid_t eid = hash(key);
	printf("##del## key %s, eid %d\n", key.c_str(), eid);
	lc->acquire((lock_protocol::lockid_t)eid);
	ec->remove(eid);
	lc->release((lock_protocol::lockid_t)eid);
	return ydb_protocol::OK;
}

extent_protocol::extentid_t ydb_server::hash(std::string key)
{
	extent_protocol::extentid_t eid = 0;
	for (int i = 0; i < key.size(); i++)
		eid = eid * 65599 + key[i];
	return eid % 1024;
}
