#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
	bzero(blocks, sizeof(blocks));
}

void disk::read_block(blockid_t id, char *buf)
{
	memcpy(buf, blocks[id], BLOCK_SIZE);
}

void disk::write_block(blockid_t id, const char *buf)
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
	blockid_t bitmap_blockid = BITMAP_START + byteI / BLOCK_SIZE;
	char buf[BLOCK_SIZE];
	d->read_block(bitmap_blockid, buf);
	int bufI = byteI % BLOCK_SIZE;
	unsigned char change = 1 << (id % 8);
	assert(status && !(change & buf[bufI]) || !status && (change & buf[bufI]));
	status ? buf[bufI] += change : buf[bufI] -= change;
	d->write_block(bitmap_blockid, buf);
}

typedef std::set<blockid_t>::iterator block_iter;
typedef std::set<inodeid_t>::iterator inode_iter;

blockid_t block_manager::alloc_block()
{
	block_iter it = freed_data.begin();
	blockid_t id = *it;
	freed_data.erase(it);
	// save_bitmap(false, true, id);
	return id;
}

void block_manager::free_block(blockid_t id)
{
	freed_data.insert(id);
	// save_bitmap(false, false, id);
}

inodeid_t block_manager::alloc_inode()
{
	inode_iter it = freed_inode.begin();
	inodeid_t id = *it;
	freed_inode.erase(it);
	// save_bitmap(true, true, id);
	return id;
}

void block_manager::free_inode(inodeid_t id)
{
	freed_inode.insert(id);
	// save_bitmap(true, false, id);
}

void block_manager::read_data(blockid_t id, char *buf)
{
	d->read_block(BBLOCK(id), buf);
}
void block_manager::write_data(blockid_t id, char *buf)
{
	d->write_block(BBLOCK(id), buf);
}
void block_manager::read_inode(inodeid_t id, char *buf)
{
	d->read_block(IBLOCK(id), buf);
}
void block_manager::write_inode(inodeid_t id, char *buf)
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

inodeid_t inode_manager::create_file(extent_protocol::types type)
{
	inodeid_t inum = bm->alloc_inode();
	char buf[BLOCK_SIZE];
	bm->read_inode(inum, buf);
	inode *ino = (inode *)buf + inum % IPB;
	ino->type = type;
	ino->size = 0;
	ino->atime = ino->ctime = ino->mtime = time(NULL);
	bm->write_inode(inum, buf);
	return inum;
}

bool inode_manager::getBlockIdList(blockid_t *blocks, int start, int end, blockid_t **blockIdList)
{
	if (end <= NDIRECT)
	{
		*blockIdList = &(blocks[start]);
		return false;
	}
	else
	{
		blockid_t indirectBlock[NINDIRECT];
		bm->read_data(blocks[NDIRECT], (char *)indirectBlock);
		blockid_t *tmp = (blockid_t *)malloc(sizeof(blockid_t) * (end - start));
		*blockIdList = tmp;
		if (start < NDIRECT)
		{
			memcpy((char *)tmp, (char *)&(blocks[start]), sizeof(blockid_t) * (NDIRECT - start));
			memcpy((char *)(tmp + NDIRECT - start), (char *)indirectBlock, sizeof(blockid_t) * (end - NDIRECT));
		}
		else
			memcpy((char *)tmp, (char *)(indirectBlock + start - NDIRECT), sizeof(blockid_t) * (end - start));
		return true;
	}
}

void inode_manager::read_file(inodeid_t inum, std::string &buf, int size, int offset)
{
	// ino->atime = time(NULL);
	// bm->write_inode(inum, inodeBuf);
	if (size == 0)
	{
		buf = "";
		return;
	}
	char inodeBuf[BLOCK_SIZE];
	bm->read_inode(inum, inodeBuf);
	inode *ino = (inode *)inodeBuf + inum % IPB;
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	if (size < 0)
		size = ino->size;
	int startBlock = offset / BLOCK_SIZE, endBlock = MIN(CEIL_DIV(offset + size, BLOCK_SIZE), allBlockN);
	int readSize = (offset + size <= ino->size) ? size : (ino->size - offset);
	int blockN = endBlock - startBlock;
	char *blocksBuf = (char *)malloc(sizeof(char) * blockN * BLOCK_SIZE);
	blockid_t *blockIdList = NULL;
	bool needFree = getBlockIdList(ino->blocks, startBlock, endBlock, &blockIdList);
	char *tmp = blocksBuf;
	for (int i = 0; i < blockN; i++)
	{
		bm->read_data(blockIdList[i], tmp);
		tmp += BLOCK_SIZE;
	}
	buf.assign(blocksBuf + offset % BLOCK_SIZE, readSize);
	free(blocksBuf);
	if (needFree)
		free(blockIdList);
}

void inode_manager::read_blocks(inode *ino, int startBlock, int endBlock, char *result)
{
	unsigned blockL[MAXFILE], *blockList, indirect[NINDIRECT];
	if (endBlock <= NDIRECT)
		blockList = ino->blocks;
	else
	{
		blockList = blockL;
		for (int i = startBlock; i < NDIRECT; i++)
			blockList[i] = ino->blocks[i];
		bm->read_data(ino->blocks[NDIRECT], (char *)indirect);
		for (int i = NDIRECT; i < endBlock; i++)
			blockList[i] = indirect[i - NDIRECT];
	}
	char *tmp = result;
	for (int i = startBlock; i < endBlock; i++)
	{
		bm->read_data(blockList[i], tmp);
		tmp += BLOCK_SIZE;
	}
}

void inode_manager::write_file(uint32_t inum, const char *buf, int size, int offset, int free_end)
{
	char inodeBuf[BLOCK_SIZE];
	bm->read_inode(inum, inodeBuf);
	inode *ino = (inode *)inodeBuf + inum % IPB;
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	int startBlock = MIN(offset / BLOCK_SIZE, ino->size / BLOCK_SIZE), endBlock = CEIL_DIV(offset + size, BLOCK_SIZE);
	assert((unsigned)endBlock <= MAXFILE);
	char *buff = (char *)calloc((endBlock - startBlock) * BLOCK_SIZE, sizeof(char));
	if (startBlock < allBlockN)
		read_blocks(ino, startBlock, MIN(allBlockN, endBlock), buff);
	memcpy(buff + MAX(0, offset / BLOCK_SIZE - startBlock) * BLOCK_SIZE + offset % BLOCK_SIZE, buf, size);
	char *tmp = buff;
	unsigned indirect[NINDIRECT];
	if (endBlock > NDIRECT && allBlockN <= NDIRECT)
		ino->blocks[NDIRECT] = bm->alloc_block();
	if (endBlock > NDIRECT)
		bm->read_data(ino->blocks[NDIRECT], (char *)indirect);
	for (int i = startBlock; i < endBlock; i++)
	{
		if (i < allBlockN)
			bm->write_data((i < NDIRECT) ? ino->blocks[i] : indirect[i - NDIRECT], tmp);
		else
		{
			unsigned id = bm->alloc_block();
			bm->write_data(id, tmp);
			(i < NDIRECT) ? ino->blocks[i] = id : indirect[i - NDIRECT] = id;
		}
		tmp += BLOCK_SIZE;
	}
	if (free_end)
	{
		for (int i = endBlock; i < allBlockN; i++)
			(i < NDIRECT) ? bm->free_block(ino->blocks[i]) : bm->free_block(indirect[i - NDIRECT]);
		if (endBlock <= NDIRECT && allBlockN > NDIRECT)
			bm->free_block(ino->blocks[NDIRECT]);
	}
	if (free_end || offset + size > ino->size)
		ino->size = offset + size;
	ino->mtime = ino->ctime = ino->atime = time(NULL);
	bm->write_inode(inum, inodeBuf);
	if (endBlock > NDIRECT)
		bm->write_data(ino->blocks[NDIRECT], (char *)indirect);
	free(buff);
}

void inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
	char buf[BLOCK_SIZE];
	bm->read_inode(inum, buf);
	inode *ino = (inode *)buf + inum % IPB;
	memcpy(&a, ino, sizeof(extent_protocol::attr));
	// ino->atime = time(NULL);
	// bm->write_inode(inum, buf);
}

void inode_manager::remove_file(uint32_t inum)
{
	char buf[BLOCK_SIZE];
	bm->read_inode(inum, buf);
	inode *ino = (inode *)buf + inum % IPB;
	int blockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	for (int i = 0; i < MIN(blockN, NDIRECT); i++)
		bm->free_block(ino->blocks[i]);
	unsigned indirect[NINDIRECT];
	if (blockN > NDIRECT)
	{
		bm->read_data(ino->blocks[NDIRECT], (char *)indirect);
		for (int i = 0; i < blockN - NDIRECT; i++)
			bm->free_block(indirect[i]);
		bm->free_block(ino->blocks[NDIRECT]);
	}
	ino->type = (extent_protocol::types)0;
	bm->write_inode(inum, buf);
	bm->free_inode(inum);
	return;
}