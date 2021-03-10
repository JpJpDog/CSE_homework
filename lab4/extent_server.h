// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"
#include "inode_manager.h"

class extent_server
{
protected:
    inode_manager *im;
    typedef std::pair<extent_protocol::extentid_t, std::vector<int> > owner_pair_t;
    typedef std::map<extent_protocol::extentid_t, std::vector<int> > owner_map_t;
    typedef owner_map_t::iterator owner_iter_t;
    owner_map_t owner_map;

    inline int get_portid(const std::string &host_id)
    {
        char host[20], tmp[10];
        sscanf(host_id.c_str(), "%[^:]:%s", host, tmp);
        return atoi(tmp);
    }

public:
    extent_server();

    int create(std::string host_id, uint32_t type, extent_protocol::extentid_t &id);
    int put(extent_protocol::extentid_t eid, std::string host_id, std::string, uint32_t &type);
    int get(extent_protocol::extentid_t eid, std::string host_id, std::string &);
    int remove(extent_protocol::extentid_t eid, std::string host_id, int &);
};

#endif
