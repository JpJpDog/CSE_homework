// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "extent_server.h"

class extent_client
{
private:
    static int last_port;
    std::string host_id;
    enum extent_status
    {
        EXCLUSIVE,
        SHARED,
        MODIFIED,
    };
    struct extent_t
    {
        std::string data;
        extent_protocol::attr attr;
        extent_status status;
    };
    typedef std::map<extent_protocol::extentid_t, extent_t *> extent_map_t;
    typedef std::pair<extent_protocol::extentid_t, extent_t *> extent_pair_t;
    typedef extent_map_t::iterator extent_iter_t;
    extent_map_t extent_map;
    extent_protocol::status pull_helper(extent_protocol::extentid_t eid, extent_iter_t &iter);

    rpcc *cl;

public:
    extent_client(std::string dst);

    extent_protocol::status create(uint32_t type, extent_protocol::extentid_t &eid);
    extent_protocol::status get(extent_protocol::extentid_t eid, std::string &buf);
    extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
    extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
    extent_protocol::status remove(extent_protocol::extentid_t eid);

    extent_protocol::status invalidate_handler(extent_protocol::extentid_t eid, int &);
    extent_protocol::status share_handler(extent_protocol::extentid_t eid, std::string &r);
};

#endif