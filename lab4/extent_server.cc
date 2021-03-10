#include "extent_server.h"
#include "handle.h"

extent_server::extent_server()
{
    im = new inode_manager();
}

int extent_server::create(std::string host_id, uint32_t type, extent_protocol::extentid_t &eid)
{
    eid = im->create_file(type);
    if (eid == -1)
        return extent_protocol::RPCERR;
    int port_id = get_portid(host_id);
    owner_map.insert(owner_pair_t(eid, std::vector<int>(1, port_id)));
    return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t eid, std::string host_id, std::string &r)
{
    // printf("##%s get %d\n", host_id.c_str(), eid);
    eid &= 0x7fffffff;
    eid %= INODE_NUM;
    int port_id = get_portid(host_id);
    owner_iter_t iter = owner_map.find(eid);
    extent_protocol::get_result result;
    result.is_share = false;
    if (iter != owner_map.end())
    {
        result.is_share = true;
        if (iter->second.size() == 1)
        {
            std::ostringstream stream;
            stream << *iter->second.begin();
            std::string dst_host = "127.0.0.1:" + stream.str();
            handle h(dst_host);
            rpcc *cl = h.safebind();
            assert(cl != NULL);
            std::string share_r;
            // printf("## make %s share %d\n", dst_host.c_str(), eid);
            extent_protocol::status ret = cl->call(rextent_protocol::share, eid, share_r);
            VERIFY(ret == extent_protocol::OK);
            rextent_protocol::share_result share_result;
            rextent_protocol::to_struct(share_r, share_result);
            if (share_result.update)
            {
                // printf("## share need update\n");
                im->write_file(eid, share_result.data.c_str(), share_result.data.size(), NULL);
            }
        }
        iter->second.push_back(port_id);
    }
    else
        owner_map.insert(owner_pair_t(eid, std::vector<int>(1, port_id)));
    int size = 0;
    char *cbuf = NULL;
    im->read_file(eid, &cbuf, &size, &result.attr);
    if (size == 0)
        result.data = "";
    else
    {
        result.data.assign(cbuf, size);
        free(cbuf);
    }
    r = extent_protocol::to_str(result);
    return extent_protocol::OK;
}

int extent_server::put(extent_protocol::extentid_t eid, std::string host_id, std::string buf, uint32_t &type)
{
    int something;
    // printf("## %s put %d\n", host_id.c_str(), eid);
    eid &= 0x7fffffff;
    eid %= INODE_NUM;
    int port_id = get_portid(host_id);
    owner_iter_t iter = owner_map.find(eid);
    if (iter != owner_map.end())
    {
        for (std::vector<int>::iterator it = iter->second.begin(); it != iter->second.end(); it++)
        {
            if (*it == port_id)
                continue;
            std::ostringstream stream;
            stream << *it;
            std::string dst_host = "127.0.0.1:" + stream.str();
            handle h(dst_host);
            rpcc *cl = h.safebind();
            assert(cl != NULL);
            extent_protocol::status ret = cl->call(rextent_protocol::invalidate, eid, something);
            VERIFY(ret == extent_protocol::OK);
        }
        iter->second.clear();
        iter->second.push_back(port_id);
    }
    else
        owner_map.insert(owner_pair_t(eid, std::vector<int>(1, port_id)));
    im->write_file(eid, buf.c_str(), buf.size(), &type);
    return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t eid, std::string host_id, int &)
{
    // printf("## %s remove %d\n", host_id.c_str(), eid);
    int something;
    eid &= 0x7fffffff;
    eid %= INODE_NUM;
    int port_id = get_portid(host_id);
    owner_iter_t iter = owner_map.find(eid);
    if (iter != owner_map.end())
    {
        for (std::vector<int>::iterator it = iter->second.begin(); it != iter->second.end(); it++)
        {
            if (*it == port_id)
                continue;
            std::ostringstream stream;
            stream << *it;
            std::string dst_host = "127.0.0.1:" + stream.str();
            handle h(dst_host);
            rpcc *cl = h.safebind();
            assert(cl != NULL);
            extent_protocol::status ret = cl->call(rextent_protocol::invalidate, eid, something);
            VERIFY(ret == extent_protocol::OK);
        }
        owner_map.erase(iter);
    }
    im->remove_file(eid);
    return extent_protocol::OK;
}
