// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "extent_protocol.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client()
{
	ec = new extent_client();
}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
	ec = new extent_client();
	if (ec->put(1, "") != extent_protocol::OK)
		printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
	std::istringstream ist(n);
	unsigned long long finum;
	ist >> finum;
	return finum;
}

std::string
yfs_client::filename(inum inum)
{
	std::ostringstream ost;
	ost << inum;
	return ost.str();
}

int yfs_client::getType(inum inum)
{
	extent_protocol::attr a;
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		printf("error getting attr\n");
		return false;
	}
	return a.type;
}

int yfs_client::getfile(inum inum, fileinfo &fin)
{
	int r = OK;

	printf("getfile %016llx\n", inum);
	extent_protocol::attr a;
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		r = IOERR;
		goto release;
	}

	fin.atime = a.atime;
	fin.mtime = a.mtime;
	fin.ctime = a.ctime;
	fin.size = a.size;
	printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
	return r;
}

int yfs_client::getdir(inum inum, dirinfo &din)
{
	int r = OK;

	printf("getdir %016llx\n", inum);
	extent_protocol::attr a;
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		r = IOERR;
		goto release;
	}
	din.atime = a.atime;
	din.mtime = a.mtime;
	din.ctime = a.ctime;

release:
	return r;
}

#define EXT_RPC(xx)                                                \
	do                                                             \
	{                                                              \
		if ((xx) != extent_protocol::OK)                           \
		{                                                          \
			printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
			r = IOERR;                                             \
			goto release;                                          \
		}                                                          \
	} while (0)

// Only support set size of attr
int yfs_client::setattr(inum ino, size_t size)
{
	ec->put(ino, "", size, 1);
	return OK;
}

int yfs_client::kreate(inum parent, const char *name, int file_type, inum &ino_out)
{
	std::list<dirent> direntList;
	readdir(parent, direntList);
	for (std::list<dirent>::iterator it = direntList.begin(); it != direntList.end(); it++)
	{
		if (strcmp(it->name.c_str(), name) == 0)
			return EXIST;
	}
	extent_protocol::extentid_t id;
	ec->create(file_type, id);
	ino_out = id;
	entry newE;
	strcpy(newE.name, name);
	newE.inum = id;
	std::string buf;
	buf.assign((char *)&newE, ENTRY_SIZE);
	ec->put(parent, buf, ENTRY_SIZE * direntList.size());

	entry ttt;
	std::string str;
	ec->get(parent, str);
	memcpy((char *)&ttt, str.c_str(), ENTRY_SIZE);
	return OK;
}

int yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
	return kreate(parent, name, extent_protocol::T_FILE, ino_out);
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
	return kreate(parent, name, extent_protocol::T_DIR, ino_out);
}

int yfs_client::mklink(inum parent, const char *name, const char *link, inum &ino_out)
{
	int status;
	if ((status = kreate(parent, name, extent_protocol::T_LINK, ino_out)) != extent_protocol::OK)
		return status;
	ec->put(ino_out, std::string(link));
	return status;
}

int yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
	std::list<dirent> direntList;
	readdir(parent, direntList);
	int type = getType(parent);
	if (type != extent_protocol::T_DIR)
		return IOERR;
	else
	{
		for (std::list<dirent>::iterator it = direntList.begin(); it != direntList.end(); it++)
		{
			if (strcmp(it->name.c_str(), name) == 0)
			{
				ino_out = it->inum;
				found = true;
				return OK;
			}
		}
		found = false;
		return OK;
	}
}

int yfs_client::readdir(inum dir, std::list<dirent> &list)
{
	std::string buf;
	ec->get(dir, buf);
	entry *e = (entry *)buf.c_str();
	int fileN = buf.size() / ENTRY_SIZE;
	for (int i = 0; i < fileN; i++)
	{
		dirent d;
		d.inum = e[i].inum;
		d.name = std::string(e[i].name);
		list.push_back(d);
	}
	return OK;
}

int yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
	ec->get(ino, data, size, off);
	return OK;
}

int yfs_client::write(inum ino, size_t size, off_t off, const char *data,
					  size_t &bytes_written)
{
	std::string buf;
	buf.assign(data, size);
	ec->put(ino, buf, off);
	return OK;
}
int yfs_client::unlink(inum parent, const char *name)
{
	std::string buf;
	ec->get(parent, buf);
	int fileN = buf.size() / ENTRY_SIZE;
	entry *e = (entry *)buf.c_str();
	bool found = false;
	int i;
	for (i = 0; i < fileN; i++)
	{
		if (strcmp(e[i].name, name) == 0)
		{
			found = true;
			ec->remove(e[i].inum);
			break;
		}
	}
	if (!found)
		return NOENT;
	std::string buff;
	if (i == fileN - 1)
		buff = "";
	else
		buff.assign((char *)&e[i + 1], ENTRY_SIZE * (fileN - 1 - i));
	ec->put(parent, buff, ENTRY_SIZE * i, 1);
	return OK;
}
