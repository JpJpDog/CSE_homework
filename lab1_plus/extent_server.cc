// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server()
{
	im = new inode_manager();
}

int extent_server::create(uint32_t type, extent_protocol::extentid_t &id)
{
	id = im->create_file((extent_protocol::types)type);
	return extent_protocol::OK;
}

int extent_server::put(extent_protocol::extentid_t id, std::string &buf, int offset, int free_end)
{
	id &= 0x7fffffff;
	const char *cbuf = buf.c_str();
	int size = buf.size();
	im->write_file(id, cbuf, size, offset, free_end);
	return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf, int size, int offset)
{
	id &= 0x7fffffff;
	im->read_file(id, buf, size, offset);
	return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
	id &= 0x7fffffff;
	im->getattr(id, a);
	return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
	id &= 0x7fffffff;
	im->remove_file(id);
	return extent_protocol::OK;
}
