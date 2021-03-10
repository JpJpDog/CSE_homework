可以用rdtsc命令测试精确时间
```c
__u64 rdtsc()
{
    __u32 lo, hi;
    __asm__ __volatile__(
        "rdtsc"
        : "=a"(lo), "=d"(hi));
    return (__u64)hi << 32 | lo;
}

```
返回值的差值就是运行的cpu周期。
  
主要改进点：
1. 去掉所有的printf的log，提升巨大
2. 尽量用memcpy代替自己手写的循环，提升巨大。
3. 在各个地方减少深拷贝。尽量减少malloc，既防止内存泄露，由加快速度。malloc在rdtsc测试中较慢。
4. 把read 和write的offset和size放到inode_manager 一层，但对这个测试没有用。现在看，这样可能不利于缓存的设计，以后再改回去。
5. 注意到fuse总是要一个文件的type，而且这个只能每次都metadata获得，而且非常容易缓存。所以在yfs_client中以inum为key缓存16个type，当删除这个文件时删除缓存。提升略微。
6. 其实本来还考虑用链表的形式缓存目录内容，也是因为这个类型比较固定，比较好缓存。而且每次都要从block中读目录内容。但是可能是因为需要多次malloc，速度反而下降了，所以没放进去。
7. 将free_inode,free_blockId改为set类型，方便查找，并且不是每次分配都写到磁盘上
