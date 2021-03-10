#ifndef ydb_server_occ_h
#define ydb_server_occ_h

#include <string>
#include <map>
#include "extent_client.h"
#include "lock_protocol.h"
#include "lock_client.h"
#include "lock_client_cache.h"
#include "ydb_protocol.h"
#include "ydb_server.h"

class ydb_server_occ : public ydb_server
{
private:
	struct TValue
	{
		int generation;
		bool readen, written;
		std::string value;
		TValue(int g, bool r, bool w, const std::string &s)
			: generation(g), readen(r), written(w), value(s) {}
	};
	typedef std::map<extent_protocol::extentid_t, TValue> VMap;
	typedef std::pair<extent_protocol::extentid_t, TValue> VPair;
	typedef VMap::iterator VIter;
	typedef std::map<ydb_protocol::transaction_id, VMap> TMap;
	typedef std::pair<ydb_protocol::transaction_id, VMap> TPair;
	typedef TMap::iterator TIter;
	ydb_protocol::transaction_id nextTransactionId;
	TMap tMap;
	lock_protocol::lockid_t tMapLockId;
	lock_protocol::lockid_t commitLockId;

	static int getGeneration(const std::string &str);
	static void composeValue(int g, const std::string &, std::string &);
	static void decomposeValue(const std::string &, int &, std::string &);
	ydb_protocol::status write(ydb_protocol::transaction_id, extent_protocol::extentid_t, const std::string &);

public:
	ydb_server_occ(std::string, std::string);
	~ydb_server_occ();
	ydb_protocol::status transaction_begin(int, ydb_protocol::transaction_id &);
	ydb_protocol::status transaction_commit(ydb_protocol::transaction_id, int &);
	ydb_protocol::status transaction_abort(ydb_protocol::transaction_id, int &);
	ydb_protocol::status get(ydb_protocol::transaction_id, const std::string, std::string &);
	ydb_protocol::status set(ydb_protocol::transaction_id, const std::string, const std::string, int &);
	ydb_protocol::status del(ydb_protocol::transaction_id, const std::string, int &);
};

#endif
