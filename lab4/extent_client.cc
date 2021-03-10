#include "extent_client.h"
#include "rpc.h"
#include <sstream>

int extent_client::last_port = 0;

extent_client::extent_client(std::string dst)
{
    sockaddr_in dstsock;
    make_sockaddr(dst.c_str(), &dstsock);
    cl = new rpcc(dstsock);
    if (cl->bind() != 0)
        printf("extent_client: bind failed\n");
    srand(time(NULL) ^ last_port);
    int port_id = last_port = ((rand() % 32000) | (0x1 << 10));
    rpcs *extent_client_rpcs = new rpcs(port_id);
    std::ostringstream stream;
    stream << port_id;
    host_id = "127.0.0.1:" + stream.str();
    printf("## host_id %s\n", host_id.c_str());
    extent_client_rpcs->reg(rextent_protocol::invalidate, this, &extent_client::invalidate_handler);
    extent_client_rpcs->reg(rextent_protocol::share, this, &extent_client::share_handler);
}

extent_protocol::status
extent_client::create(uint32_t type, extent_protocol::extentid_t &eid)
{
    extent_protocol::status ret = cl->call(extent_protocol::create, host_id, type, eid);
    VERIFY(ret == extent_protocol::OK);
    // printf("##create %d\n", eid);
    extent_t *e = new extent_t();
    e->attr.atime = e->attr.mtime = e->attr.ctime = time(0);
    e->attr.type = type;
    e->attr.size = 0;
    e->status = EXCLUSIVE;
    extent_map.insert(extent_pair_t(eid, e));
    return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
    extent_iter_t iter = extent_map.find(eid);
    if (iter == extent_map.end())
        pull_helper(eid, iter);
    else
        iter->second->attr.atime = time(0);
    buf = iter->second->data;
    // printf("## get %d\n", eid);
    return extent_protocol::OK;
}

extent_protocol::status extent_client::getattr(extent_protocol::extentid_t eid, extent_protocol::attr &attr)
{
    // printf("## getattr %d\n", eid);
    extent_iter_t iter = extent_map.find(eid);
    if (iter == extent_map.end())
        pull_helper(eid, iter);
    else
        iter->second->attr.atime = time(0);
    attr = iter->second->attr;
    return extent_protocol::OK;
}

extent_protocol::status extent_client::pull_helper(extent_protocol::extentid_t eid, extent_iter_t &iter)
{
    // printf("##pull %d\n", eid);
    extent_t *e = new extent_t();
    std::string r;
    extent_protocol::status ret = cl->call(extent_protocol::get, eid, host_id, r);
    VERIFY(ret == extent_protocol::OK);
    extent_protocol::get_result result;
    extent_protocol::to_struct(r, result);
    e->data = result.data;
    e->attr = result.attr;
    e->status = result.is_share ? SHARED : EXCLUSIVE;
    extent_map.insert(extent_pair_t(eid, e));
    iter = extent_map.find(eid);
    return extent_protocol::OK;
}

extent_protocol::status extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
    // printf("## put %d\n", eid);
    extent_iter_t iter = extent_map.find(eid);
    if (iter != extent_map.end())
    {
        switch (iter->second->status)
        {
        case EXCLUSIVE:
            iter->second->status = MODIFIED;
        case MODIFIED:
        {
            extent_t *e = iter->second;
            e->data = buf;
            e->attr.size = buf.size();
            e->attr.atime = e->attr.ctime = e->attr.mtime = time(0);
            return extent_protocol::OK;
        }
        }
    }
    else
    {
        extent_map.insert(extent_pair_t(eid, new extent_t()));
        iter = extent_map.find(eid);
    }
    uint32_t type;
    extent_protocol::status ret = cl->call(extent_protocol::put, eid, host_id, buf, type);
    VERIFY(ret == extent_protocol::OK);
    extent_t *e = iter->second;
    e->data = buf;
    e->status = EXCLUSIVE;
    e->attr.size = buf.size();
    e->attr.atime = e->attr.mtime = e->attr.ctime = time(0);
    e->attr.type = type;
    return extent_protocol::OK;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
    // printf("##remove %d\n", eid);
    int something;
    extent_iter_t iter = extent_map.find(eid);
    if (iter != extent_map.end())
    {
        free(iter->second);
        extent_map.erase(iter);
    }
    extent_protocol::status ret = cl->call(extent_protocol::remove, eid, host_id, something);
    VERIFY(ret == extent_protocol::OK);
    return ret;
}

extent_protocol::status extent_client::invalidate_handler(extent_protocol::extentid_t eid, int &)
{
    // printf("##invalidate %d\n", eid);
    extent_iter_t iter = extent_map.find(eid);
    assert(iter != extent_map.end());
    free(iter->second);
    extent_map.erase(iter);
    return extent_protocol::OK;
}

extent_protocol::status extent_client::share_handler(extent_protocol::extentid_t eid, std::string &r)
{
    // printf("##share %d\n", eid);
    extent_iter_t iter = extent_map.find(eid);
    assert(iter != extent_map.end());
    rextent_protocol::share_result result;
    result.update = false;
    if (iter->second->status == MODIFIED)
    {
        result.update = true;
        result.data = iter->second->data;
    }
    iter->second->status = SHARED;
    r = rextent_protocol::to_str(result);
    return extent_protocol::OK;
}
