// extent wire protocol

#ifndef extent_protocol_h
#define extent_protocol_h

#include "rpc.h"

class extent_protocol
{
public:
	typedef int status;
	typedef unsigned long long extentid_t;
	enum xxstatus
	{
		OK,
		RPCERR,
		NOENT,
		IOERR
	};
	enum rpc_numbers
	{
		put = 0x5001,
		get,
		getattr,
		remove,
		create,
		flush,
	};

	enum types
	{
		T_DIR = 1,
		T_FILE,
		T_SYMLINK,
	};

	struct attr
	{
		uint32_t type;
		unsigned int atime;
		unsigned int mtime;
		unsigned int ctime;
		unsigned int size;
	};

	struct get_result
	{
		std::string data;
		extent_protocol::attr attr;
		bool is_share;
	};
	static std::string to_str(const get_result &r) { return std::string((char *)&r.attr, sizeof(r.attr)) + std::string((char *)&r.is_share, sizeof(bool)) + r.data; }
	static void to_struct(const std::string &str, get_result &r)
	{
		memcpy((char *)&r.attr, str.c_str(), sizeof(r.attr));
		memcpy((char *)&r.is_share, str.c_str() + sizeof(r.attr), sizeof(bool));
		r.data = str.substr(sizeof(r.attr) + sizeof(bool));
	}
};

class rextent_protocol
{
public:
	enum rpc_numbers
	{
		invalidate = 0x6001,
		share,
	};
	struct share_result
	{
		bool update;
		std::string data;
	};
	static std::string to_str(const share_result &r) { return std::string((char *)&r.update, sizeof(bool)) + r.data; }
	static void to_struct(const std::string &str, share_result &r)
	{
		memcpy((char *)&r.update, str.c_str(), sizeof(bool));
		r.data = str.substr(sizeof(bool));
	}
};

inline unmarshall &
operator>>(unmarshall &u, extent_protocol::attr &a)
{
	u >> a.type;
	u >> a.atime;
	u >> a.mtime;
	u >> a.ctime;
	u >> a.size;
	return u;
}

inline marshall &
operator<<(marshall &m, extent_protocol::attr a)
{
	m << a.type;
	m << a.atime;
	m << a.mtime;
	m << a.ctime;
	m << a.size;
	return m;
}

#endif
