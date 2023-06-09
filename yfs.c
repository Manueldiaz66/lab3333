#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include <stdlib.h>
#include <string.h>
#include "yfs.h"
#include "hash_table.h"
#include "message.h"
#include <comp421/iolib.h>


#define LOADFACTOR 1.5

linkedNode *firstFreeInode = NULL;
linkedNode *firstFreeBlock = NULL;

int freeInodeCount = 0;
int freeBlockCount = 0;


queue *cacheInodeQueue;
struct hash_table *inodeTable;
int inodeCacheSize = 0;

queue *cacheBlockQueue;
struct hash_table *blockTable;
int blockCacheSize = 0;

cacheItem *
removeItemFromFrontOfQueue(queue *queue)
{
        cacheItem *firstItem = queue->firstItem;
        if (firstItem == NULL) {
                return NULL;
        }
        if (queue->firstItem->nextItem == NULL) {
                queue->lastItem = NULL;
        }

        queue->firstItem->prevItem = NULL;
        queue->firstItem = queue->firstItem->nextItem;
        if (queue->firstItem != NULL)
                queue->firstItem->prevItem = NULL;
        return firstItem;
}


void
removeItemFromQueue(queue *queue, cacheItem *item)
{
        if (item->prevItem == NULL) {
                removeItemFromFrontOfQueue(queue);
        } else {
                if (item->nextItem == NULL) {
                        queue->lastItem = item->prevItem;
                }
                item->prevItem->nextItem = item->nextItem;
                if (item->nextItem != NULL) {
                        item->nextItem->prevItem = item->prevItem;
                }
        }
}

void
addItemToEndOfQueue(cacheItem *item, queue *queue)
{
        // if the queue is empty
        if (queue->firstItem == NULL) {
                if (queue == cacheBlockQueue)
                        item->nextItem = NULL;
                item->prevItem = NULL;
                queue->lastItem = item;
                queue->firstItem = item;
        } else {    // if the queue is nonempty
                queue->lastItem->nextItem = item;
                item->prevItem = queue->lastItem;
                queue->lastItem = item;
                queue->lastItem->nextItem = NULL;
        }
}

bool
isEqual(char *path, char dirEntryName[]) {
        int i = 0;
        while (i < DIRNAMELEN) {
                if ((path[i] == '/' || path[i] == '\0') && dirEntryName[i] == '\0') {
                        return true;
                }
                if (path[i] != dirEntryName[i]) {
                        return false;
                }
                i++;
        }
        return true;
}

void
saveBlock(int blockNumber) {
        // mark the block as dirty
        //void *block = getBlock(blockNumber);
        //(void)block;
        cacheItem *blockItem = (cacheItem *)hash_table_lookup(blockTable, blockNumber);
        blockItem->dirty = true;
}

void *
getBlock(int blockNumber) {
        //TracePrintf(1, "GETTING BLOCK #%d\n", blockNumber);
        // First check to see if Block is in the cache using hashmap
        // If it is, remove it from the middle of the block queue add it to the front
        // return the pointer to it
        cacheItem *blockItem = (cacheItem *)hash_table_lookup(blockTable, blockNumber);

        if (blockItem != NULL) {
                removeItemFromQueue(cacheBlockQueue, blockItem);
                addItemToEndOfQueue(blockItem, cacheBlockQueue);
                return blockItem->addr;
        }

        // If the block is not in the cache

        // If the cache is full, remove the LRU block from the end of the queue,
        // and get the block number
        // Use the block number to remove it from the hashmap
        if (blockCacheSize == BLOCK_CACHESIZE) {
                cacheItem *lruBlockItem = removeItemFromFrontOfQueue(cacheBlockQueue);
                int lruBlockNum = lruBlockItem->number;
                WriteSector(lruBlockNum, lruBlockItem->addr);
                blockCacheSize--;
                hash_table_remove(blockTable, lruBlockNum, NULL, NULL);
                destroyCacheItem(lruBlockItem);
        }

        // allocate space for the new block, read it from disk
        // Add the new block to the front of the LRU queue and add it to the hashmap
        // and then return the pointer to the new block
        //TracePrintf(1, "block was NOT in cache\n");
        void *block = malloc(BLOCKSIZE);
        ReadSector(blockNumber, block);
        cacheItem *newItem = malloc(sizeof(cacheItem));
        newItem->number = blockNumber;
        newItem->addr = block;
        newItem->dirty = false;

        addItemToEndOfQueue(newItem, cacheBlockQueue);
        blockCacheSize++;
        hash_table_insert(blockTable, blockNumber, newItem);
        return block;
}

void
saveInode(int inodeNum) {
//    struct inode *inode = getInode(inodeNum);
//    (void)inode;
        // Lookup the inode ptr in the hashmap
        cacheItem *inodeItem = (cacheItem *)hash_table_lookup(inodeTable, inodeNum);

        // mark the inode as dirty
        inodeItem->dirty = true;
}

struct inode*
getInode(int inodeNum) {
        // First, check to see if inode is in the cache using hashmap
        // If it is, remove it from the middle of the inode queue and add it to the front
        // return the pointer to the inode
        cacheItem *nodeItem = (cacheItem *)hash_table_lookup(inodeTable, inodeNum);
        if (nodeItem != NULL) {
                removeItemFromQueue(cacheInodeQueue, nodeItem);
                addItemToEndOfQueue(nodeItem, cacheInodeQueue);
                return nodeItem->addr;
        }

        // If it is not in the cache

        // If the cache is full:
        // Get the lru inode in the cache, remove it from the hashmap
        // get the block number corresponding to lru inode
        // get the block corresponding to this block number
        // get the correct address corresponding to this inode within that block
        // copy the contents of the lru inode into this address
        // call save block on that block
        if (inodeCacheSize == INODE_CACHESIZE) {
                cacheItem *lruInode = removeItemFromFrontOfQueue(cacheInodeQueue);
                int lruInodeNum = lruInode->number;
                inodeCacheSize--;
                hash_table_remove(inodeTable, lruInodeNum, NULL, NULL);
                int lruBlockNum = (lruInodeNum / INODESPERBLOCK) + 1;

                void *lruBlock = getBlock(lruBlockNum);
                void *inodeAddrInBlock = (lruBlock + (lruInodeNum - (lruBlockNum - 1) * INODESPERBLOCK) * INODESIZE);

                memcpy(inodeAddrInBlock, lruInode->addr, sizeof(struct inode));
                saveBlock(lruBlockNum);

                destroyCacheItem(lruInode);
        }

        // Get the block number corresponding to this new inode
        int blockNum = (inodeNum / INODESPERBLOCK) + 1;

        // Get the block address for this inode
        void *blockAddr = getBlock(blockNum);

        // Look up the inodes address within the block
        struct inode *newInodeAddrInBlock = (struct inode *)(blockAddr + (inodeNum - (blockNum - 1) * INODESPERBLOCK) * INODESIZE);

        // Copy the contents of the inode into a newly allocated inode
        struct inode *inodeCpy = malloc(sizeof(struct inode));
        struct cacheItem *inodeItem = malloc(sizeof(struct cacheItem));
        memcpy(inodeCpy, newInodeAddrInBlock, sizeof(struct inode));
        inodeItem->addr = inodeCpy;
        inodeItem->number = inodeNum;

        // Add this inode to the front of the LRU queue and add it to the hashmap
        addItemToEndOfQueue(inodeItem, cacheInodeQueue);
        inodeCacheSize++;
        hash_table_insert(inodeTable, inodeNum, inodeItem);

        // return the address of the new inode
        return inodeItem->addr;
}

void
destroyCacheItem(cacheItem *item) {
        free(item->addr);
        free(item);
}

void *
getBlockForInode(int inodeNumber) {
        int blockNumber = (inodeNumber / INODESPERBLOCK) + 1;
        return getBlock(blockNumber);
}

/*
 * Expects: inode, n
 * Results: mallocs that block into memory
 */
int
getNthBlock(struct inode *inode, int n, bool allocateIfNeeded) {
        bool isOver = false;
        if (n >= NUM_DIRECT + BLOCKSIZE / (int)sizeof(int)) {
                return 0;
        }
        if (n*BLOCKSIZE >= inode->size)
        {
                isOver = true;
        }
        if (isOver && !allocateIfNeeded) {
                return 0;
        }
        if (n < NUM_DIRECT) {
                if (isOver) {
                        inode->direct[n] = getNextFreeBlockNum();
                }
                // if getNextFreeBlockNum returned 0, return 0
                return inode->direct[n];
        }
        //search the direct blocks
        int *indirectBlock = getBlock(inode->indirect);
        if (isOver) {
                // if getNextFreeBlockNum returned 0, return 0
                indirectBlock[n - NUM_DIRECT] = getNextFreeBlockNum();
        }
        int blockNum = indirectBlock[n - NUM_DIRECT];
        return blockNum;
}

/**
 *
 * @param path the file path to get the inode number for
 * @param inodeStartNumber the inode to start looking for the next part
 *       of the file path in
 * @return the inode number of the path, or 0 if it's an invalid path
 */
int
getInodeNumberForPath(char *path, int inodeStartNumber)
{


        //get the inode number for the first file in path
        // ex: if path is "/a/b/c.txt" get the indoe # for "a"
        int nextInodeNumber = 0;

        //Get inode corresponding to inodeStartNumber
        void *block = getBlockForInode(inodeStartNumber);
        struct inode *inode = getInode(inodeStartNumber);
        if (inode->type == INODE_DIRECTORY) {
                // go get the directory entry in this directory
                // that has that name
                int blockNum;
                int offset = getDirectoryEntry(path, inodeStartNumber, &blockNum, false);
                if (offset != -1) {
                        block = getBlock(blockNum);
                        struct dir_entry * dir_entry = (struct dir_entry *) ((char *) block + offset);
                        nextInodeNumber = dir_entry->inum;
                }
        } else if (inode->type == INODE_REGULAR) {
                return 0;
        }
        char *nextPath = path;
        if (nextInodeNumber == 0) {
                // Return error
                return 0;
        }
        while (nextPath[0] != '/') {
                // base case
                if (nextPath[0] == '\0') {
                        inode = getInode(nextInodeNumber);
                        return nextInodeNumber;

                }
                nextPath += sizeof (char);
        }
        while (nextPath[0] == '/') {
                nextPath += sizeof (char);
        }
        if (nextPath[0] == '\0') {
                return nextInodeNumber;
        }
        //nextPath += sizeof (char);
        inode = getInode(nextInodeNumber);
        return getInodeNumberForPath(nextPath, nextInodeNumber);
}


/*
 * Set inode to free and add it to the list
 */
void
freeUpInode(int inodeNum) {
        // get address of this inode
        struct inode *inode = getInode(inodeNum);

        // modify the type of inode to free
        inode->type = INODE_FREE;

        addToFreeList(inodeNum, 1);

        saveInode(inodeNum);
}

int
getNextFreeInodeNum() {
        if (firstFreeInode == NULL) {
                return 0;
        }
        int inodeNum = firstFreeInode->value;
        struct inode *inode = getInode(inodeNum);
        inode->reuse++;
        saveInode(inodeNum);
        firstFreeInode = firstFreeInode->next;
        return inodeNum;
}

void
addToFreeList(int val, int type) {
        linkedNode *newHead = malloc(sizeof(linkedNode));
        newHead->value = val;
        if(type){
                newHead->next = firstFreeInode;
                freeInodeCount++;
                firstFreeInode = newHead;
        }
        else{
                newHead->next = firstFreeBlock;
                freeBlockCount++;
                firstFreeBlock= newHead;
        }
}

int getNextFreeBlockNum() {
        if (firstFreeBlock == NULL) {
                return 0;
        }
        int blockNum = firstFreeBlock->value;
        firstFreeBlock = firstFreeBlock->next;
        return blockNum;
}


void
clearFile(struct inode *inode, int inodeNum) {
        int i = 0;
        int blockNum;
        while ((blockNum = getNthBlock(inode, i++, false)) != 0) {
                addToFreeList(blockNum, 0);
        }
        inode->size = 0;
        saveInode(inodeNum);
}

/*
 * Returns offset within blocknum block
 */
int
getDirectoryEntry(char *pathname, int inodeStartNumber, int *blockNumPtr, bool createIfNeeded) {
        int freeEntryOffset = -1;
        int freeEntryBlockNum = 0;
        void * currentBlock;
        struct dir_entry *currentEntry;
        struct inode *inode = getInode(inodeStartNumber);
        int i = 0;
        int blockNum = getNthBlock(inode, i, false);
        int currBlockNum = 0;
        int totalSize = sizeof (struct dir_entry);
        bool isFound = false;
        while (blockNum != 0 && !isFound) {
                currentBlock = getBlock(blockNum);
                currentEntry = (struct dir_entry *) currentBlock;
                while (totalSize <= inode->size
                       && ((char *) currentEntry < ((char *) currentBlock + BLOCKSIZE)))
                {
                        if (freeEntryOffset == -1 && currentEntry->inum == 0) {
                                freeEntryBlockNum = blockNum;
                                freeEntryOffset = (int)((char *)currentEntry - (char *)currentBlock);
                        }

                        //check the currentEntry fileName to see if it matches
                        TracePrintf(1, "current entry->name - %s\n", currentEntry->name);
                        if (isEqual(pathname, currentEntry->name)) {
                                isFound = true;
                                break;
                        }

                        //increment current entry
                        currentEntry = (struct dir_entry *) ((char *) currentEntry + sizeof (struct dir_entry));
                        totalSize += sizeof (struct dir_entry);
                }
                if (isFound) {
                        break;
                }
                currBlockNum = blockNum;
                blockNum = getNthBlock(inode, ++i, false);
        }
        *blockNumPtr = blockNum;

        if (isFound) {
                int offset = (int)((char *)currentEntry - (char *)currentBlock);
                return offset;
        }
        if (createIfNeeded) {
                if (freeEntryBlockNum != 0) {
                        *blockNumPtr = freeEntryBlockNum;
                        return freeEntryOffset;
                }
                if (inode->size % BLOCKSIZE == 0) {
                        // we're at the bottom edge of the block, so
                        // we need to allocate a new block
                        blockNum = getNthBlock(inode, i, true);
                        currentBlock = getBlock(blockNum);
                        inode->size += sizeof(struct dir_entry);
                        struct dir_entry * newEntry = (struct dir_entry *) currentBlock;
                        newEntry->inum = 0;
                        saveBlock(blockNum);
                        saveInode(inodeStartNumber);
                        *blockNumPtr = blockNum;
                        return 0;
                }
                inode->size += sizeof(struct dir_entry);
                saveInode(inodeStartNumber);
                currentEntry->inum = 0;
                saveBlock(currBlockNum);
                *blockNumPtr = currBlockNum;
                int offset = (int)((char *)currentEntry - (char *)currentBlock);
                return offset;
        }
        return -1;
}

int
getContainingDirectory(char *pathname, int currentInode, char **filenamePtr) {
        // error checking
        int i;
        for (i = 0; i < MAXPATHNAMELEN; i++) {
                if (pathname[i] == '\0') {
                        break;
                }
        }
        if (i == MAXPATHNAMELEN) {
                return ERROR;
        }

        // adjust pathname
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }

        // Truncate the pathname to NOT include the name of the file to create
        int lastSlashIndex = 0;
        i = 0;
        while ((pathname[i] != '\0') && (i < MAXPATHNAMELEN)) {
                if (pathname[i] == '/') {
                        lastSlashIndex = i;
                }
                i++;
        }

        if (lastSlashIndex != 0) {
                char path[lastSlashIndex + 1];
                for (i = 0; i < lastSlashIndex; i++) {
                        path[i] = pathname[i];
                }
                path[i] = '\0';

                char *filename = pathname + (sizeof(char) * (lastSlashIndex + 1));
                *filenamePtr = filename;
                // Get the inode of the directory the file should be created in
                int dirInodeNum = getInodeNumberForPath(path, currentInode);
                if (dirInodeNum == 0) {
                        return ERROR;
                }
                return dirInodeNum;
        } else {

                *filenamePtr = pathname;
                return currentInode;
        }
}

int
ServerOpen(char *pathname, int currentInode) {
        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }
        int inodenum = getInodeNumberForPath(pathname, currentInode);
        if (inodenum == 0) {
                return ERROR;
        }
        return inodenum;
}

int
ServerCreate(char *pathname, int currentInode, int inodeNumToSet) {
        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }
        int i;
        for (i = 0; i < MAXPATHNAMELEN; i++) {
                if (pathname[i] == '\0') {
                        break;
                }
        }
        if (i == MAXPATHNAMELEN) {
                return ERROR;
        }

        i = 0;
        while ((pathname[i] != '\0') && (i < MAXPATHNAMELEN)) {
                if (pathname[i] == '/') {
                        if (i+1 > MAXPATHNAMELEN || pathname[i+1] == '\0') {
                                return ERROR;
                        }
                }
                i++;
        }
        TracePrintf(1, "Creating %s in %d\n", pathname, currentInode);
        char *filename;
        int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);
        TracePrintf(1, "containind dirInodenum = %d\n", dirInodeNum);
        if (dirInodeNum == ERROR) {
                return ERROR;
        }

        struct inode *dirInode = getInode(dirInodeNum);

        if (dirInode->type != INODE_DIRECTORY) {
                return ERROR;
        }
        // Search all directory entries of that inode for the file name to create
        int blockNum;
        TracePrintf(1, "getting directory entry: %s in inode %d\n", filename, dirInodeNum);
        int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
        TracePrintf(1, "offset = %d, blockNum = %d\n", offset, blockNum);
        void *block = getBlock(blockNum);

        struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);
        // If the file exists, get the inode, set its size to zero, and return
        // that inode number to user
        int inodeNum = dir_entry->inum;
        if (inodeNum != 0) {
                if (inodeNumToSet != -1) {
                        return ERROR;
                }
                int inodeNum = dir_entry->inum;
                struct inode *inode = getInode(inodeNum);
                clearFile(inode, inodeNum);
                saveInode(inodeNum);
                return inodeNum;
        }

        // If the file does not exist, find the first free directory entry, get
        // a new inode number from free list, get that inode, change the info on
        // that inode and directory entry (name, type), then return the inode number
        for (i = 0; i<DIRNAMELEN; i++) {
                dir_entry->name[i] = '\0';
        }
        for (i = 0; filename[i] != '\0'; i++) {
                dir_entry->name[i] = filename[i];
        }
        TracePrintf(1, "new directory entry name: %s\n", dir_entry->name);
        if (inodeNumToSet == CREATE_NEW) {
                TracePrintf(1, "Creating new!\n");
                inodeNum = getNextFreeInodeNum();
                TracePrintf(1, "new inodeNum = %d\n", inodeNum);
                dir_entry->inum = inodeNum;
                saveBlock(blockNum);
                struct inode *inode = getInode(inodeNum);
                inode->type = INODE_REGULAR;
                inode->size = 0;
                inode->nlink = 1;
                saveInode(inodeNum);
                return inodeNum;
        } else {
                dir_entry->inum = inodeNumToSet;
                saveBlock(blockNum);
                return inodeNumToSet;
        }
}

int
ServerRead(int inodeNum, void *buf, int size, int byteOffset, int pid) {
        if (buf == NULL || size < 0 || byteOffset < 0 || inodeNum <= 0) {
                return ERROR;
        }
        struct inode *inode = getInode(inodeNum);

        if (byteOffset > inode->size) {
                return ERROR;
        }

        int bytesLeft = size;
        if (inode->size - byteOffset < size) {
                bytesLeft = inode->size - byteOffset;
        }

        int returnVal = bytesLeft;

        int blockOffset = byteOffset % BLOCKSIZE;

        int bytesToCopy = BLOCKSIZE - blockOffset;

        int i;
        for (i = byteOffset / BLOCKSIZE; bytesLeft > 0; i++) {
                int blockNum = getNthBlock(inode, i, false);
                if (blockNum == 0) {
                        return ERROR;
                }
                void *currentBlock = getBlock(blockNum);

                if (bytesLeft < bytesToCopy) {
                        bytesToCopy = bytesLeft;
                }

                if (CopyTo(pid, buf, (char *)currentBlock + blockOffset, bytesToCopy) == ERROR)
                {
                        TracePrintf(1, "error copying %d bytes to pid %d\n", bytesToCopy, pid);
                        return ERROR;
                }

                buf += bytesToCopy;
                blockOffset = 0;
                bytesLeft -= bytesToCopy;
                bytesToCopy = BLOCKSIZE;
        }

        return returnVal;
}

int
ServerWrite(int inodeNum, void *buf, int size, int byteOffset, int pid) {

        struct inode *inode = getInode(inodeNum);
        if (inode->type != INODE_REGULAR) {
                return ERROR;
        }

        int bytesLeft = size;

        int returnVal = bytesLeft;

        int blockOffset = byteOffset % BLOCKSIZE;

        int bytesToCopy = BLOCKSIZE - blockOffset;

        int i;
        for (i = byteOffset / BLOCKSIZE; bytesLeft > 0; i++) {
                int blockNum = getNthBlock(inode, i, true);
                if (blockNum == 0) {
                        return ERROR;
                }
                void *currentBlock = getBlock(blockNum);

                if (bytesLeft < bytesToCopy) {
                        bytesToCopy = bytesLeft;
                }

                if (CopyFrom(pid, (char *)currentBlock + blockOffset, buf, bytesToCopy) == ERROR)
                {
                        TracePrintf(1, "error copying %d bytes from pid %d\n", bytesToCopy, pid);
                        return ERROR;
                }

                buf += bytesToCopy;
                saveBlock(blockNum);

                blockOffset = 0;
                bytesLeft -= bytesToCopy;
                bytesToCopy = BLOCKSIZE;
                int bytesWrittenSoFar = size - bytesLeft;
                if (bytesWrittenSoFar + byteOffset > inode->size) {
                        inode->size = bytesWrittenSoFar + byteOffset;
                }
        }
        saveInode(inodeNum);
        return returnVal;
}

int
ServerLink(char *oldName, char *newName, int currentInode) {
        if (oldName == NULL || newName == NULL || currentInode <= 0) {
                return ERROR;
        }
        if (oldName[0] == '/') {
                oldName += sizeof(char);
                currentInode = ROOTINODE;
        }
        int oldNameNodeNum = getInodeNumberForPath(oldName, currentInode);
        struct inode *inode = getInode(oldNameNodeNum);
        if (inode->type == INODE_DIRECTORY || oldNameNodeNum == 0) {
                return ERROR;
        }

        if (newName[0] == '/') {
                newName += sizeof(char);
                currentInode = ROOTINODE;
        }
        if (ServerCreate(newName, currentInode, oldNameNodeNum) == ERROR) {
                return ERROR;
        }
        inode->nlink++;
        saveInode(oldNameNodeNum);

        return 0;
}

int
ServerUnlink(char *pathname, int currentInode) {
        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }

        // Get the containind directory
        char *filename;
        int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);

        struct inode *dirInode = getInode(dirInodeNum);
        if (dirInode->type != INODE_DIRECTORY) {
                return ERROR;
        }

        int blockNum;
        int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, false);
        if (offset == -1) {
                return ERROR;
        }
        void *block = getBlock(blockNum);

        // Get the directory entry associated with the path
        struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);

        // Get the inode associated with the directory entry
        int inodeNum = dir_entry->inum;
        struct inode *inode = getInode(inodeNum);

        // Decrease nlinks by 1
        inode->nlink--;

        // If nlinks == 0, clear the file
        if (inode->nlink == 0) {
                clearFile(inode, inodeNum);
        }

        saveInode(inodeNum);

        // Set the inum to zero
        dir_entry->inum = 0;
        saveBlock(blockNum);

        return 0;
}

int
ServerMkDir(char *pathname, int currentInode) {

        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }
        char *filename;
        int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);
        // Search all directory entries of that inode for the file name to create
        int blockNum;
        int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
        void *block = getBlock(blockNum);

        struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);

        // return error if this directory already exists
        if (dir_entry->inum != 0) {
                return ERROR;
        }

        memset(&dir_entry->name, '\0', DIRNAMELEN);
        int i;
        for (i = 0; filename[i] != '\0'; i++) {
                dir_entry->name[i] = filename[i];
        }

        int inodeNum = getNextFreeInodeNum();
        dir_entry->inum = inodeNum;
        saveBlock(blockNum);
        block = getBlockForInode(inodeNum);

        struct inode *inode = getInode(inodeNum);
        inode->type = INODE_DIRECTORY;
        inode->size = 2 * sizeof (struct dir_entry);
        inode->nlink = 1;

        int firstDirectBlockNum = getNextFreeBlockNum();
        void *firstDirectBlock = getBlock(firstDirectBlockNum);
        inode->direct[0] = firstDirectBlockNum;

        struct dir_entry *dir1 = (struct dir_entry *)firstDirectBlock;
        dir1->inum = inodeNum;
        dir1->name[0] = '.';

        struct dir_entry *dir2 = (struct dir_entry *)((char *)dir1 + sizeof(struct dir_entry));
        dir2->inum = dirInodeNum;
        dir2->name[0] = '.';
        dir2->name[1] = '.';

        saveBlock(firstDirectBlockNum);

        saveInode(inodeNum);
        return 0;
}

int
ServerRmDir(char *pathname, int currentInode) {
        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }
        int inodeNum = getInodeNumberForPath(pathname, currentInode);
        if (inodeNum == ERROR) {
                return ERROR;
        }
        struct inode *inode = getInode(inodeNum);

        if (inode->size > (int)(2*sizeof(struct dir_entry))) {
                return ERROR;
        }

        clearFile(inode, inodeNum);
        addToFreeList(inodeNum, 1);


        char *filename;
        int dirInodeNum = getContainingDirectory(pathname, currentInode, &filename);

        int blockNum;
        int offset = getDirectoryEntry(filename, dirInodeNum, &blockNum, true);
        void *block = getBlock(blockNum);

        // Get the directory entry associated with the path
        struct dir_entry *dir_entry = (struct dir_entry *) ((char *)block + offset);

        // Set the inum to zero
        dir_entry->inum = 0;
        saveBlock(blockNum);
        return 0;
}

int
ServerChDir(char *pathname, int currentInode) {
        if (pathname == NULL || currentInode <= 0) {
                return ERROR;
        }
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }
        int inode = getInodeNumberForPath(pathname, currentInode);
        if (inode == 0) {
                return ERROR;
        }
        return inode;
}

int
ServerStat(char *pathname, int currentInode, struct Stat *statbuf, int pid) {
        if (pathname == NULL || currentInode <= 0 || statbuf == NULL) {
                return ERROR;
        }
        if (pathname[0] == '/') {
                while (pathname[0] == '/')
                        pathname += sizeof(char);
                currentInode = ROOTINODE;
        }
        int inodeNum = getInodeNumberForPath(pathname, currentInode);
        if (inodeNum == 0) {
                return ERROR;
        }
        struct inode *inode = getInode(inodeNum);

        struct Stat stat;
        stat.inum = inodeNum;
        stat.nlink = inode->nlink;
        stat.size = inode->size;
        stat.type = inode->type;

        if (CopyTo(pid, statbuf, &stat, sizeof(struct Stat)) == ERROR) {
                TracePrintf(1, "error copying %d bytes to pid %d\n", sizeof(struct Stat), pid);
                return ERROR;
        }

        return 0;
}

int
ServerSync(void) {
        TracePrintf(1, "About to sync all dirty blocks and inodes\n");
        // First sync all dirty blocks
        cacheItem *currBlockItem = cacheBlockQueue->firstItem;
        while (currBlockItem != NULL) {
                //TracePrintf(1, "currBlockItem->num = %d\n", currBlockItem->number);
                if (currBlockItem->dirty) {
                        //write this block back to disk
                        WriteSector(currBlockItem->number, currBlockItem->addr);
                }
                currBlockItem = currBlockItem->nextItem;
        }

        // Now sync all dirty inodes
        cacheItem *currInodeItem = cacheInodeQueue->firstItem;
        while (currInodeItem != NULL) {
                if (currInodeItem->dirty) {
                        int inodeNum = currInodeItem->number;
                        int blockNum = (inodeNum / INODESPERBLOCK) + 1;

                        void *block = getBlock(blockNum);
                        void *inodeAddrInBlock = (block + (inodeNum - (blockNum - 1) * INODESPERBLOCK) * INODESIZE);

                        memcpy(inodeAddrInBlock, currInodeItem->addr, sizeof(struct inode));
                        WriteSector(blockNum, block);
                }
                currInodeItem = currInodeItem->nextItem;
        }
        TracePrintf(1, "Done syncing\n");
        return 0;
}

int
ServerShutdown(void) {
        ServerSync();
        TracePrintf(1, "About to shutdown the Server file system server\n");
        Exit(0);
}

int
ServerSeek(int inodeNum, int offset, int whence, int currentPosition) {
        struct inode *inode = getInode(inodeNum);
        int size = inode->size;
        if (currentPosition > size || currentPosition < 0) {
                return ERROR;
        }
        if (whence == SEEK_SET) {
                if (offset < 0 || offset > size) {
                        return ERROR;
                }
                return offset;
        }
        if (whence == SEEK_CUR) {
                if (currentPosition + offset > size || currentPosition + offset < 0) {
                        return ERROR;
                }
                return currentPosition + offset;
        }
        if (whence == SEEK_END) {
                if (offset > 0 || size + offset < 0) {
                        return ERROR;
                }
                return size + offset;
        }
        return ERROR;
}

int
main(int argc, char **argv)
{
        (void) argc;
        (void) argv;

        cacheInodeQueue = malloc(sizeof(queue));
        cacheInodeQueue->firstItem = NULL;
        cacheInodeQueue->lastItem = NULL;

        cacheBlockQueue = malloc(sizeof(queue));
        cacheBlockQueue->firstItem = NULL;
        cacheBlockQueue->lastItem = NULL;
        inodeTable = hash_table_create(LOADFACTOR, INODE_CACHESIZE + 1);
        blockTable = hash_table_create(LOADFACTOR, BLOCK_CACHESIZE + 1);

        int blockNum = 1;


        void *block = getBlock(blockNum);

        struct fs_header header = *((struct fs_header*) block);
        TracePrintf(1, "num_blocks: %d, num_inodes: %d\n", header.num_blocks,
                    header.num_inodes);

        // create array indexed by block number
        bool takenBlocks[header.num_blocks];
        // initialize each item to false
        memset(takenBlocks, false, header.num_blocks * sizeof(bool));
        // sector 0 is taken
        takenBlocks[0] = true;

        // for each block that contains inodes
        int inodeNum = ROOTINODE;
        while (inodeNum < header.num_inodes) {
                // for each inode, if it's free, add it to the free list
                for (; inodeNum < INODESPERBLOCK * blockNum; inodeNum++) {
                        struct inode *inode = getInode(inodeNum);
                        if (inode->type == INODE_FREE) {
                                addToFreeList(inodeNum, 1);
                        } else {
                                // keep track of all these blocks as taken
                                int i = 0;
                                int blockNum;
                                while((blockNum = getNthBlock(inode, i++, false)) != 0) {
                                        takenBlocks[blockNum] = true;
                                }
                        }
                }
                blockNum++;
                block = getBlock(blockNum);
        }
        TracePrintf(1, "initialized free inode list with %d free inodes\n",
                    freeInodeCount);

        // for each element in the block array
        int i;
        for (i = 0; i < header.num_blocks; i++) {
                if (!takenBlocks[i]) {
                        addToFreeList(i, 0);
                        freeBlockCount++;
                }
        }
        TracePrintf(1, "initialized free block list with %d free blocks\n",
                    freeBlockCount);

        if (Register(FILE_SERVER) != 0) {
                TracePrintf(1, "error registering file server as a service\n");
                Exit(1);
        };



        if (argc > 1) {
                if (Fork() == 0) {
                        Exec(argv[1], argv + 1);
                } else {
                        for (;;) {
                                processRequest();
                        }
                }
        }

        return (0);
}