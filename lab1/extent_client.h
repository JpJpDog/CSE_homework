// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "extent_server.h"

class extent_client
{
private:
	extent_server *es;

public:
	extent_client();

	extent_protocol::status create(uint32_t type, extent_protocol::extentid_t &eid);
	extent_protocol::status get(extent_protocol::extentid_t eid, std::string &buf, int size = -1, int offset = 0);
	extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
	extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf, int offset = 0, int free_end = 0);
	extent_protocol::status remove(extent_protocol::extentid_t eid);
};

#endif
