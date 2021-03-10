#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
	bzero(blocks, sizeof(blocks));
}

void disk::read_block(uint32_t id, char *buf)
{
	memcpy(buf, blocks[id], BLOCK_SIZE);
}

void disk::write_block(uint32_t id, const char *buf)
{
	memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

block_manager::block_manager()
{
	d = new disk();

	sb.size = BLOCK_SIZE * BLOCK_NUM;
	sb.nblocks = BLOCK_NUM;
	sb.ninodes = INODE_NUM;
	char tmp[BLOCK_SIZE] = {0};
	tmp[0] = 1;
	d->write_block(BITMAP_START, (const char *)&tmp);
	load_bitmap();
}

void block_manager::load_bitmap()
{
	char buf[BLOCK_SIZE];
	int K = 0;
	int imap = 1;
	for (int i = 0; i < BITMAP_BLOCK_NUM; i++)
	{
		d->read_block(BITMAP_START + i, buf);
		for (int j = 0; j < BLOCK_SIZE; j++)
		{
			char ch = buf[j];
			for (int k = 0; k < 8; k++)
			{
				if (!(ch & (1 << k)))
					imap ? freed_inode.insert(K) : freed_data.insert(K);
				K++;
			}
			if (K == INODE_NUM && imap)
				K = imap = 0;
			if (K == DATA_BLOCK_NUM && !imap)
				return;
		}
	}
}

void block_manager::save_bitmap(bool is_inode, bool status, uint32_t id)
{
	int byteI = id / 8 + (is_inode ? 0 : INODE_BITMAP_BYTE_NUM);
	uint32_t bitmap_blockid = BITMAP_START + byteI / BLOCK_SIZE;
	char buf[BLOCK_SIZE];
	d->read_block(bitmap_blockid, buf);
	int bufI = byteI % BLOCK_SIZE;
	unsigned char change = 1 << (id % 8);
	assert(status && !(change & buf[bufI]) || !status && (change & buf[bufI]));
	status ? buf[bufI] += change : buf[bufI] -= change;
	d->write_block(bitmap_blockid, buf);
}

typedef std::set<uint32_t>::iterator iter;

// Allocate a free disk block.
uint32_t block_manager::alloc_block()
{
	iter it = freed_data.begin();
	if (it == freed_data.end())
		return -1;
	unsigned id = *it;
	freed_data.erase(it);
	// save_bitmap(false, true, id);
	return id;
}

void block_manager::free_block(uint32_t id)
{
	freed_data.insert(id);
	// save_bitmap(false, false, id);
}

uint32_t block_manager::alloc_inode()
{
	iter it = freed_inode.begin();
	if (it == freed_inode.end())
		return -1;
	uint32_t id = *it;
	freed_inode.erase(it);
	// save_bitmap(true, true, id);
	return id;
}

void block_manager::free_inode(uint32_t id)
{
	freed_inode.insert(id);
	// save_bitmap(true, false, id);
}

void block_manager::read_data(unsigned id, char *buf)
{
	d->read_block(BBLOCK(id), buf);
}
void block_manager::write_data(unsigned id, char *buf)
{
	d->write_block(BBLOCK(id), buf);
}
void block_manager::read_inode(unsigned id, char *buf)
{
	d->read_block(IBLOCK(id), buf);
}
void block_manager::write_inode(unsigned id, char *buf)
{
	d->write_block(IBLOCK(id), buf);
}

// inode layer -----------------------------------------

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

inode_manager::inode_manager()
{
	bm = new block_manager();
	uint32_t root_dir = create_file(extent_protocol::T_DIR);
	if (root_dir != 1)
	{
		printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
		exit(0);
	}
}

uint32_t inode_manager::create_file(uint32_t type)
{
	uint32_t inum = bm->alloc_inode();
	char inodeBlock[BLOCK_SIZE];
	bm->read_inode(inum, inodeBlock);
	inode *ino = (inode *)inodeBlock + inum % IPB;
	ino->type = type;
	ino->size = 0;
	ino->atime = ino->ctime = ino->mtime = time(NULL);
	bm->write_inode(inum, inodeBlock);
	return inum;
}

void inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
	char inodeBlock[BLOCK_SIZE];
	bm->read_inode(inum, inodeBlock);
	inode *ino = (inode *)inodeBlock + inum % IPB;
	memcpy(&a, ino, sizeof(a));
	ino->atime = time(NULL);
	bm->write_inode(inum, inodeBlock);
}

bool inode_manager::get_blockids(inode *ino, uint32_t **blockids1, int *allBlockN1, char *indirectBlock)
{
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	*allBlockN1 = allBlockN;
	if (allBlockN <= NDIRECT)
	{
		*blockids1 = ino->blocks;
		return false;
	}
	uint32_t *blockids = (uint32_t *)malloc(sizeof(uint32_t) * allBlockN);
	*blockids1 = blockids;
	memcpy(blockids, ino->blocks, sizeof(uint32_t) * NDIRECT);
	bm->read_data(ino->blocks[NDIRECT], indirectBlock);
	memcpy(blockids + NDIRECT, indirectBlock, sizeof(uint32_t) * (allBlockN - NDIRECT));
	return true;
}

void inode_manager::read_file(uint32_t inum, char **buf_out1, int *size1, extent_protocol::attr *a)
{
	char inodeBlock[BLOCK_SIZE], indirectBlock[BLOCK_SIZE];
	bm->read_inode(inum, inodeBlock);
	inode *ino = (inode *)inodeBlock + inum % IPB;
	ino->atime = time(NULL);
	if (a)
		memcpy(a, ino, sizeof(*a));
	bm->write_inode(inum, inodeBlock);
	*size1 = ino->size;
	if (ino->size == 0)
		return;
	int allBlockN = 0;
	uint32_t *blockids = NULL;
	bool needFree = get_blockids(ino, &blockids, &allBlockN, indirectBlock);
	char *buf_out = (char *)malloc(sizeof(char) * allBlockN * BLOCK_SIZE);
	*buf_out1 = buf_out;
	for (int i = 0; i < allBlockN; i++)
	{
		bm->read_data(blockids[i], buf_out);
		buf_out += BLOCK_SIZE;
	}
	if (needFree)
		free(blockids);
}

void inode_manager::write_file(uint32_t inum, const char *buf, int size, uint32_t *type)
{
	char inodeBlock[BLOCK_SIZE], indirectBlock[BLOCK_SIZE], lastBlock[BLOCK_SIZE];
	bm->read_inode(inum, inodeBlock);
	inode *ino = (inode *)inodeBlock + inum % IPB;
	if (type)
		*type = ino->type;
	int blockN = 0, newBlockN = CEIL_DIV(size, BLOCK_SIZE);
	assert(newBlockN <= MAXFILE);
	uint32_t *blockIds = NULL;
	bool needFree = get_blockids(ino, &blockIds, &blockN, indirectBlock);
	memcpy(lastBlock, buf + (newBlockN - 1) * BLOCK_SIZE, size - (newBlockN - 1) * BLOCK_SIZE);
	char *tmp = (char *)buf;
	for (int i = 0; i < MIN(blockN, newBlockN); i++)
	{
		if (i == newBlockN - 1)
			tmp = lastBlock;
		bm->write_data(blockIds[i], tmp);
		tmp += BLOCK_SIZE;
	}
	if (blockN > newBlockN)
	{
		for (int i = newBlockN; i < blockN; i++)
			bm->free_block(blockIds[i]);
		if (blockN > NDIRECT && newBlockN <= NDIRECT)
			bm->free_block(ino->blocks[NDIRECT]);
	}
	else if (blockN < newBlockN)
	{
		for (int i = blockN; i < MIN(newBlockN, NDIRECT); i++)
		{
			if (i == newBlockN - 1)
				tmp = lastBlock;
			uint32_t newBlockId = bm->alloc_block();
			ino->blocks[i] = newBlockId;
			bm->write_data(newBlockId, tmp);
			tmp += BLOCK_SIZE;
		}
		if (newBlockN > NDIRECT)
		{
			if (!needFree)
			{
				uint32_t indirectBlockId = bm->alloc_block();
				ino->blocks[NDIRECT] = indirectBlockId;
			}
			uint32_t *indirectBlockIds = (uint32_t *)indirectBlock;
			for (int i = MAX(NDIRECT, blockN); i < newBlockN; i++)
			{
				if (i == newBlockN - 1)
					tmp = lastBlock;
				uint32_t newBlockId = bm->alloc_block();
				indirectBlockIds[i - NDIRECT] = newBlockId;
				bm->write_data(newBlockId, tmp);
				tmp += BLOCK_SIZE;
			}
			bm->write_data(ino->blocks[NDIRECT], indirectBlock);
		}
	}
	ino->size = size;
	ino->atime = ino->ctime = ino->mtime = time(NULL);
	bm->write_inode(inum, inodeBlock);
	if (needFree)
		free(blockIds);
}

void inode_manager::remove_file(uint32_t inum)
{
	char inodeBlock[BLOCK_SIZE], indirectBlock[BLOCK_SIZE];
	bm->read_inode(inum, inodeBlock);
	inode *ino = (inode *)inodeBlock + inum % IPB;
	int blockN = 0;
	uint32_t *blockIds = NULL;
	bool needFree = get_blockids(ino, &blockIds, &blockN, indirectBlock);
	for (int i = 0; i < blockN; i++)
		bm->free_block(blockIds[i]);
	if (needFree)
	{
		bm->free_block(ino->blocks[NDIRECT]);
		free(blockIds);
	}
	ino->type = (extent_protocol::types)0;
	bm->write_inode(inum, inodeBlock);
	bm->free_inode(inum);
}
