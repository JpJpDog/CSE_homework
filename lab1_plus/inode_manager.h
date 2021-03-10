// inode layer interface.

#ifndef inode_h
#define inode_h

#include <stdint.h>
#include <set>
#include "extent_protocol.h" // TODO: delete it

#define CEIL_DIV(a, b) ((a) == 0 ? 0 : (((a)-1) / (b) + 1))

#define DISK_SIZE 1024 * 1024 * 16
#define BLOCK_SIZE 512
#define BLOCK_NUM (DISK_SIZE / BLOCK_SIZE)

typedef uint32_t blockid_t;

// disk layer -----------------------------------------

class disk
{
private:
	unsigned char blocks[BLOCK_NUM][BLOCK_SIZE];

public:
	disk();
	void read_block(blockid_t id, char *buf);
	void write_block(blockid_t id, const char *buf);
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
	extent_protocol::types type;
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

#define IBLOCK(i) (BITMAP_START + BITMAP_BLOCK_NUM + (i) / IPB)
#define BBLOCK(i) (BITMAP_START + BITMAP_BLOCK_NUM + INODE_BLOCK_NUM + i)

typedef uint32_t inodeid_t;

class block_manager
{
private:
	disk *d;
	std::set<blockid_t> freed_data;
	std::set<inodeid_t> freed_inode;
	void load_bitmap();
	void save_bitmap(bool is_inode, bool status, uint32_t id);

public:
	block_manager();
	struct superblock sb;

	blockid_t alloc_block();
	void free_block(blockid_t id);
	inodeid_t alloc_inode();
	void free_inode(inodeid_t id);
	void read_data(unsigned id, char *buf);
	void write_data(unsigned id, char *buf);
	void read_inode(unsigned id, char *buf);
	void write_inode(unsigned id, char *buf);
};

// inode layer -----------------------------------------

class inode_manager
{
private:
	block_manager *bm;
	bool getBlockIdList(blockid_t *blocks, int start, int end, blockid_t **blockIdList1);
	void read_blocks(inode *ino, int startBlock, int endBlock, char *result);

public:
	inode_manager();
	inodeid_t create_file(extent_protocol::types type);
	void read_file(inodeid_t inum, std::string &buf, int size, int offset);
	void write_file(inodeid_t inum, const char *buf, int size, int offset, int free_end);
	void remove_file(inodeid_t inum);
	void getattr(inodeid_t inum, extent_protocol::attr &a);
};

#endif