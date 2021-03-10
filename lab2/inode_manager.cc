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

void block_manager::load_bitmap()
{
	char buf[BLOCK_SIZE];
	unsigned I = 0;
	int imap = 1;
	for (unsigned i = 0; i < BITMAP_BLOCK_NUM; i++)
	{
		d->read_block(BITMAP_START + i, buf);
		for (int i = 0; i < BLOCK_SIZE; i++)
		{
			unsigned char ch = buf[i];
			for (unsigned j = 0; j < 8; j++)
			{
				int is_used = ch & (1 << j);
				if (!is_used)
				{
					if (imap)
						freed_inode.insert(I);
					else
						freed_data.insert(I);
				}
				I++;
			}
			if (I == INODE_NUM && imap)
				I = imap = 0;
			if (I == DATA_BLOCK_NUM && !imap)
			{
				freed_inode.erase(0);
				save_bitmap(1, 0, 0);
				return;
			}
		}
	}
}

void block_manager::save_bitmap(int is_inode, int id, int status)
{
	int byteI = id / 8 + (is_inode ? 0 : INODE_BITMAP_BYTE_NUM);
	unsigned bitmap_blockid = BITMAP_START + byteI / BLOCK_SIZE;
	char buf[BLOCK_SIZE];
	d->read_block(bitmap_blockid, buf);
	int bufI = byteI % BLOCK_SIZE;
	int change = 1 << (id % 8);
	if (id == 0 && is_inode == 1)
		buf[bufI] = 1;
	else
		status ? buf[bufI] += change : buf[bufI] -= change;
	d->write_block(bitmap_blockid, buf);
}

typedef std::set<uint32_t>::iterator iter;

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
	iter it = freed_data.begin();
	unsigned id = *it;
	printf("### alloc block %d\n", id);
	freed_data.erase(it);
	save_bitmap(0, id, 1);
	return id;
}

void block_manager::free_block(uint32_t id)
{
	printf("### free block %d\n", id);
	freed_data.insert(id);
	save_bitmap(0, id, 0);
}

uint32_t block_manager::alloc_inode()
{
	iter it = freed_inode.begin();
	uint32_t id = *it;
	printf("### alloc inode %d\n", id);
	freed_inode.erase(it);
	save_bitmap(1, id, 1);
	return id;
}

void block_manager::free_inode(uint32_t id)
{
	printf("### free inode %d\n", id);
	freed_inode.insert(id);
	save_bitmap(1, id, 0);
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
	d = new disk();

	// format the disk
	sb.size = BLOCK_SIZE * BLOCK_NUM;
	sb.nblocks = BLOCK_NUM;
	sb.ninodes = INODE_NUM;
	load_bitmap();
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

inode_manager::inode_manager()
{
	bm = new block_manager();
	uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
	if (root_dir != 1)
	{
		printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
		exit(0);
	}
}
/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
	uint32_t id = bm->alloc_inode();
	extent_protocol::attr a;
	a.type = type;
	a.size = 0;
	a.ctime = a.atime = a.mtime = time(NULL);
	setattr(id, a);
	return id;
}

void inode_manager::free_inode(uint32_t inum)
{
	/* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */

	return;
}

/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
inode *
inode_manager::get_inode(uint32_t inum)
{
	inode *ino, *ino_disk;
	char buf[BLOCK_SIZE];

	printf("\tim: get_inode %d\n", inum);

	if (inum < 0 || inum >= INODE_NUM)
	{
		printf("\tim: inum out of range\n");
		return NULL;
	}

	bm->read_inode(inum, buf);
	// printf("%s:%d\n", __FILE__, __LINE__);

	ino_disk = (inode *)buf + inum % IPB;
	if (ino_disk->type == 0)
	{
		printf("\tim: inode not exist\n");
		return NULL;
	}

	ino = (inode *)malloc(sizeof(inode));
	*ino = *ino_disk;

	return ino;
}

void inode_manager::put_inode(uint32_t inum, inode *ino)
{
	char buf[BLOCK_SIZE];
	inode *ino_disk;

	printf("\tim: put_inode %d\n", inum);
	if (ino == NULL)
		return;

	bm->read_inode(inum, buf);
	ino_disk = (inode *)buf + inum % IPB;
	*ino_disk = *ino;
	bm->write_inode(inum, buf);
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void inode_manager::read_file(uint32_t inum, char **buf_out, int *ssize, int size, int offset)
{
	printf("### read inum: %d, size: %d, offset: %d\n", inum, size, offset);
	char inodeBuf[BLOCK_SIZE];
	bm->read_inode(inum, inodeBuf);
	inode *ino = (inode *)inodeBuf + inum % IPB;
	ino->atime = time(NULL);
	bm->write_inode(inum, inodeBuf);
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	if (size < 0)
		size = ino->size;
	if (size == 0)
	{
		*ssize = 0;
		return;
	}
	int startBlock = offset / BLOCK_SIZE, endBlock = MIN(CEIL_DIV(offset + size, BLOCK_SIZE), allBlockN);
	int ss = (endBlock <= allBlockN) ? size : ino->size - startBlock * BLOCK_SIZE;
	*ssize = ss;
	char *blocksBuf = (char *)malloc(sizeof(char) * (endBlock - startBlock) * BLOCK_SIZE);
	read_blocks(ino, startBlock, endBlock, blocksBuf);
	*buf_out = (char *)malloc(sizeof(char) * ss);
	memcpy(*buf_out, blocksBuf + offset % BLOCK_SIZE, ss);
	free(blocksBuf);
}

/* alloc/free blocks if needed */
void inode_manager::write_file(uint32_t inum, const char *buf, int size, int offset, int free_end)
{
	printf("### write inum: %d, size: %d, offset: %d\n", inum, size, offset);
	char inodeBuf[BLOCK_SIZE];
	bm->read_inode(inum, inodeBuf);
	inode *ino = (inode *)inodeBuf + inum % IPB;
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	int startBlock = MIN(offset / BLOCK_SIZE, (int)ino->size / BLOCK_SIZE), endBlock = CEIL_DIV(offset + size, BLOCK_SIZE);
	printf("### startBlock %d, endBlock %d, allBlockN %d\n", startBlock, endBlock, allBlockN);
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
	if (free_end || offset + size > (int)ino->size)
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
	ino->atime = time(NULL);
	bm->write_inode(inum, buf);
}

void inode_manager::setattr(uint32_t inum, extent_protocol::attr &a)
{
	char buf[BLOCK_SIZE];
	bm->read_inode(inum, buf);
	inode *ino = (inode *)buf + inum % IPB;
	memcpy(ino, &a, sizeof(extent_protocol::attr));
	ino->atime = ino->ctime = ino->mtime = time(NULL);
	bm->write_inode(inum, buf);
}

void inode_manager::remove_file(uint32_t inum)
{
	char buf[BLOCK_SIZE];
	bm->read_inode(inum, buf);
	inode *ino = (inode *)buf + inum % IPB;
	int allBlockN = CEIL_DIV(ino->size, BLOCK_SIZE);
	for (int i = 0; i < MIN(allBlockN, NDIRECT); i++)
		bm->free_block(ino->blocks[i]);
	unsigned indirect[NINDIRECT];
	if (allBlockN > NDIRECT)
	{
		bm->read_data(ino->blocks[NDIRECT], (char *)indirect);
		for (int i = 0; i < allBlockN - NDIRECT; i++)
			bm->free_block(indirect[i]);
		bm->free_block(ino->blocks[NDIRECT]);
	}
	ino->type = 0;
	bm->write_inode(inum, buf);
	bm->free_inode(inum);
	return;
}
