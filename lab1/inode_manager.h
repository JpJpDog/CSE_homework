// inode layer interface.

#ifndef inode_h
#define inode_h

#include <stdint.h>
#include "extent_protocol.h" // TODO: delete it

#define CEIL_DIV(a, b) ((a) == 0 ? 0 : (((a)-1) / (b) + 1))

#define DISK_SIZE 1024 * 1024 * 16
#define BLOCK_SIZE 512
#define BLOCK_NUM (DISK_SIZE / BLOCK_SIZE)

typedef uint32_t blockid_t;

typedef uint32_t inodeid_t;

// disk layer -----------------------------------------

class disk
{
private:
	unsigned char blocks[BLOCK_NUM][BLOCK_SIZE];

public:
	disk();
	void read_block(uint32_t id, char *buf);
	void write_block(uint32_t id, const char *buf);
};

// block layer -----------------------------------------

#define INODE_NUM 1024
#define NDIRECT 100
#define NINDIRECT (BLOCK_SIZE / sizeof(blockid_t))
#define MAXFILE (NDIRECT + NINDIRECT)

typedef struct superblock
{
	uint32_t size;
	uint32_t nblocks;
	uint32_t ninodes;
} superblock_t;

typedef struct inode
{
	uint32_t type;
	unsigned int atime;
	unsigned int mtime;
	unsigned int ctime;
	unsigned int size;
	blockid_t blocks[NDIRECT + 1]; // Data block addresses
} inode_t;

#define BITMAP_START 2
#define IPB (BLOCK_SIZE / sizeof(inode))
#define INODE_BLOCK_NUM CEIL_DIV(INODE_NUM, IPB)
#define INODE_BITMAP_BYTE_NUM CEIL_DIV(INODE_NUM, 8)

#define DATA_BLOCK_NUM BLOCK_NUM - INODE_BLOCK_NUM
#define DATA_BITMAP_BYTE_NUM CEIL_DIV(DATA_BLOCK_NUM, 8)

#define BITMAP_BYTE_NUM DATA_BITMAP_BYTE_NUM + INODE_BITMAP_BYTE_NUM
#define BITMAP_BLOCK_NUM CEIL_DIV(BITMAP_BYTE_NUM, BLOCK_SIZE)

#include <set>

#define IBLOCK(i) (BITMAP_START + BITMAP_BLOCK_NUM + (i) / IPB)
#define BBLOCK(i) (BITMAP_START + BITMAP_BLOCK_NUM + INODE_BLOCK_NUM + i)

class block_manager
{
private:
	disk *d;
	std::set<blockid_t> freed_data;
	std::set<inodeid_t> freed_inode;
	void load_bitmap();
	void save_bitmap(int is_inode, int id, int status);

public:
	block_manager();
	struct superblock sb;

	uint32_t alloc_block();
	void free_block(uint32_t id);
	uint32_t alloc_inode();
	void free_inode(uint32_t id);
	void read_data(unsigned id, char *buf);
	void write_data(unsigned id, char *buf);
	void read_inode(unsigned id, char *buf);
	void write_inode(unsigned id, char *buf);
	// void read_block(uint32_t id, char *buf);
	// void write_block(uint32_t id, const char *buf);
};

// inode layer -----------------------------------------

class inode_manager
{
private:
	block_manager *bm;
	struct inode *get_inode(uint32_t inum);
	void put_inode(uint32_t inum, struct inode *ino);
	void read_blocks(inode *ino, int startB, int endB, char *result);
	void setattr(uint32_t inum, extent_protocol::attr &a);
	
public:
	inode_manager();
	uint32_t alloc_inode(uint32_t type);
	void free_inode(uint32_t inum);
	void read_file(uint32_t inum, char **buf, int *ssize, int size = -1, int offset = 0);
	void write_file(uint32_t inum, const char *buf, int size, int offset = 0, int free_end = 0);
	void remove_file(uint32_t inum);
	void getattr(uint32_t inum, extent_protocol::attr &a);
};

#endif
