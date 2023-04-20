#include <stdbool.h>
#include <comp421/iolib.h>

#define INODESPERBLOCK (BLOCKSIZE / INODESIZE)
#define CREATE_NEW -1

typedef struct linkedNode linkedNode;
typedef struct freeBlock freeBlock;
typedef struct cacheItem cacheItem;
typedef struct queue queue;

struct cacheItem {
    int number;
    bool dirty;
    void *addr;
    cacheItem *prevItem;
    cacheItem *nextItem;
};

struct linkedNode {
    int value;
    struct linkedNode *next;
};

struct queue {
    cacheItem *firstItem;
    cacheItem *lastItem;
};

void *getBlock(int blockNumber);
void destroyCacheItem(cacheItem *item);
struct inode* getInode(int inodeNum);
void addToFreeList(int inodeNum, int type);

int getNextFreeBlockNum();
int getDirectoryEntry(char *pathname, int inodeStartNumber, int *blockNumPtr, bool createIfNeeded);
int ServerCreate(char *pathname, int currentInode, int inodeNumToSet);
int ServerOpen(char *pathname, int currentInode);
int ServerRead(int inodeNum, void *buf, int size, int byteOffset, int pid);
int ServerWrite(int inodeNum, void *buf, int size, int byteOffset, int pid);
int ServerLink(char *oldName, char *newName, int currentInode);
int ServerUnlink(char *pathname, int currentInode);
int ServerSymLink(char *oldname, char *newname, int currentInode);
int ServerReadLink(char *pathname, char *buf, int len, int currentInode, int pid);
int ServerMkDir(char *pathname, int currentInode);
int ServerRmDir(char *pathname, int currentInode);
int ServerChDir(char *pathname, int currentInode);
int ServerStat(char *pathname, int currentInode, struct Stat *statbuf, int pid);
int ServerSync(void);
int ServerShutdown(void);
int ServerSeek(int inodeNum, int offset, int whence, int currentPosition);