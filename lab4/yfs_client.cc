// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
	ec = new extent_client(extent_dst);
	//要改
	// Lab2: Use lock_client_cache when you test lock_cache
	// lc = new lock_client(lock_dst);
	lc = new lock_client_cache(lock_dst);
	if (ec->put(1, "") != extent_protocol::OK)
		printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum yfs_client::n2i(std::string n)
{
	std::istringstream ist(n);
	unsigned long long finum;
	ist >> finum;
	return finum;
}

std::string yfs_client::filename(inum inum)
{
	std::ostringstream ost;
	ost << inum;
	return ost.str();
}

int yfs_client::getType(inum inum)
{
	extent_protocol::attr a;
	lc->acquire((lock_protocol::lockid_t)inum);
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		lc->release((lock_protocol::lockid_t)inum);
		printf("error getting attr\n");
		return 0;
	}
	lc->release((lock_protocol::lockid_t)inum);
	return a.type;
}

int yfs_client::getfile(inum inum, fileinfo &fin)
{
	int r = OK;
	extent_protocol::attr a;
	lc->acquire((lock_protocol::lockid_t)inum);
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		r = RPCERR;
		goto release;
	}
	fin.atime = a.atime;
	fin.mtime = a.mtime;
	fin.ctime = a.ctime;
	fin.size = a.size;
release:
	lc->release((lock_protocol::lockid_t)inum);
	return r;
}

int yfs_client::getdir(inum inum, dirinfo &din)
{
	int r = OK;
	extent_protocol::attr a;
	lc->acquire((lock_protocol::lockid_t)inum);
	if (ec->getattr(inum, a) != extent_protocol::OK)
	{
		r = RPCERR;
		goto release;
	}
	din.atime = a.atime;
	din.mtime = a.mtime;
	din.ctime = a.ctime;
release:
	lc->release((lock_protocol::lockid_t)inum);
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
	int r = OK;
	lc->acquire((lock_protocol::lockid_t)ino);
	std::string oldFile, newFile;
	ec->get(ino, oldFile);
	int oldSize = oldFile.size();
	if (oldSize < size)
		newFile = oldFile + std::string(size - oldSize, 0);
	else
		newFile = oldFile.substr(0, size);
	r = ec->put(ino, newFile);
	lc->release((lock_protocol::lockid_t)ino);
	return r;
}

int yfs_client::kreate(inum parent, const char *name, int file_type, inum &ino_out)
{
	std::string dirFile;
	ec->get(parent, dirFile);
	entry *e = (entry *)dirFile.c_str();
	assert(dirFile.size() % ENTRY_SIZE == 0);
	int fileN = dirFile.size() / ENTRY_SIZE;
	for (int i = 0; i < fileN; i++)
	{
		if (strcmp(e[i].name, name) == 0)
			return EXIST;
	}
	ec->create(file_type, ino_out);
	entry newE;
	strcpy(newE.name, name);
	newE.inum = ino_out;
	std::string buf;
	buf.assign((char *)&newE, ENTRY_SIZE);
	ec->put(parent, dirFile + buf);
	return OK;
}

int yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
	lc->acquire((lock_protocol::lockid_t)parent);
	int tmp = kreate(parent, name, extent_protocol::T_FILE, ino_out);
	lc->release((lock_protocol::lockid_t)parent);
	return tmp;
}

int yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
	lc->acquire((lock_protocol::lockid_t)parent);
	int tmp = kreate(parent, name, extent_protocol::T_DIR, ino_out);
	lc->release((lock_protocol::lockid_t)parent);
	return tmp;
}

int yfs_client::mklink(inum parent, const char *name, const char *link, inum &ino_out)
{
	int status;
	lc->acquire((lock_protocol::lockid_t)parent);
	if ((status = kreate(parent, name, extent_protocol::T_SYMLINK, ino_out)) != extent_protocol::OK)
		goto RET;
	lc->acquire((lock_protocol::lockid_t)ino_out);
	ec->put(ino_out, std::string(link));
	lc->release((lock_protocol::lockid_t)ino_out);
RET:
	lc->release((lock_protocol::lockid_t)parent);
	return status;
}

int yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
	extent_protocol::attr a;
	lc->acquire((lock_protocol::lockid_t)parent);
	ec->getattr(parent, a);
	if (a.type != extent_protocol::T_DIR)
	{
		lc->release((lock_protocol::lockid_t)parent);
		return RPCERR;
	}
	std::string dirFile;
	ec->get(parent, dirFile);
	entry *e = (entry *)dirFile.c_str();
	assert(dirFile.size() % ENTRY_SIZE == 0);
	int fileN = dirFile.size() / ENTRY_SIZE;
	found = false;
	for (int i = 0; i < fileN; i++)
	{
		if (strcmp(e[i].name, name) == 0)
		{
			found = true;
			ino_out = e[i].inum;
			break;
		}
	}
	lc->release((lock_protocol::lockid_t)parent);
	return OK;
}

int yfs_client::readdir(inum dir, std::list<dirent> &list)
{
	std::string dirFile;
	lc->acquire((lock_protocol::lockid_t)dir);
	ec->get(dir, dirFile);
	entry *e = (entry *)dirFile.c_str();
	assert(dirFile.size() % ENTRY_SIZE == 0);
	int fileN = dirFile.size() / ENTRY_SIZE;
	for (int i = 0; i < fileN; i++)
	{
		dirent d;
		d.inum = e[i].inum;
		d.name = std::string(e[i].name);
		list.push_back(d);
	}
	lc->release((lock_protocol::lockid_t)dir);
	return OK;
}

int yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
	std::string allData;
	lc->acquire((lock_protocol::lockid_t)ino);
	ec->get(ino, allData);
	data = allData.substr(off, size);
	lc->release((lock_protocol::lockid_t)ino);
	return OK;
}

int yfs_client::write(inum ino, size_t size, off_t off, const char *data, size_t &bytes_written)
{
	std::string allData;
	lc->acquire((lock_protocol::lockid_t)ino);
	ec->get(ino, allData);
	if (allData.size() < off)
		allData += std::string(off - allData.size(), 0);
	int oldSize = allData.size();
	std::string newData = allData.substr(0, off) + std::string(data, size) + ((oldSize <= off + size) ? "" : allData.substr(off + size));
	ec->put(ino, newData);
	lc->release((lock_protocol::lockid_t)ino);
	return OK;
}

int yfs_client::unlink(inum parent, const char *name)
{
	std::string dirFile;
	lc->acquire((lock_protocol::lockid_t)parent);
	ec->get(parent, dirFile);
	assert(dirFile.size() % ENTRY_SIZE == 0);
	int fileN = dirFile.size() / ENTRY_SIZE;
	entry *e = (entry *)dirFile.c_str();
	bool found = false;
	int entryIndex;
	for (entryIndex = 0; entryIndex < fileN; entryIndex++)
	{
		if (strcmp(e[entryIndex].name, name) == 0)
		{
			found = true;
			lc->acquire((lock_protocol::lockid_t)e[entryIndex].inum);
			ec->remove(e[entryIndex].inum);
			lc->release((lock_protocol::lockid_t)e[entryIndex].inum);
			break;
		}
	}
	if (!found)
	{
		lc->release((lock_protocol::lockid_t)parent);
		return NOENT;
	}
	ec->put(parent, dirFile.substr(0, ENTRY_SIZE * entryIndex) + dirFile.substr(ENTRY_SIZE * (entryIndex + 1)));
	lc->release((lock_protocol::lockid_t)parent);
	return OK;
}
