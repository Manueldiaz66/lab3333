/*
 * Interaction between yfs and user library is done here
 */
#include <stdlib.h>
#include <comp421/filesystem.h>
#include <comp421/yalnix.h>
#include "message.h"
#include "yfs.h"
#include <stdlib.h>
#include <string.h>
#include "hash_table.h"
#include <comp421/iolib.h>

static char * getPathFromProcess(int pid, char *pathname);

void
processRequest()
{
        int return_value;

        struct message_generic msg_rcv;

        // receive the message as a generic type first
        int pid = Receive(&msg_rcv);
        if (pid == ERROR) {
                TracePrintf(1, "unable to receive message, shutting down\n");
                ServerShutdown();
        }
        int operationID = msg_rcv.num;
        struct message_path *standardPath;
        /*
        struct message_file *fileMessage;
        struct message_seek *seekMessage;
        struct message_stat *statMessage;
        struct message_link *linkMessage;
        struct message_generic response;
         */
        char *pathname;
        if (operationID == YFS_OPEN ||
            operationID == YFS_CREATE
            || operationID == YFS_UNLINK ||
            operationID == YFS_MKDIR
            || operationID == YFS_RMDIR ||
            operationID == YFS_CHDIR) {
                standardPath = (struct message_path *) &msg_rcv;
                pathname = getPathFromProcess(pid, standardPath->pathname);
                if (msg_rcv.num == YFS_OPEN) {
                        return_value = ServerOpen(pathname,
                                                  standardPath->current_inode);
                } else if (msg_rcv.num == YFS_CREATE) {
                        return_value = ServerCreate(pathname,
                                                    standardPath->current_inode,
                                                    CREATE_NEW);
                } else if (msg_rcv.num == YFS_UNLINK) {
                        return_value = ServerUnlink(pathname,
                                                    standardPath->current_inode);
                } else if (msg_rcv.num == YFS_MKDIR) {
                        return_value = ServerMkDir(pathname,
                                                   standardPath->current_inode);
                } else if (msg_rcv.num == YFS_RMDIR) {
                        return_value = ServerRmDir(pathname,
                                                   standardPath->current_inode);
                } else if (msg_rcv.num == YFS_CHDIR) {
                        return_value = ServerChDir(pathname,
                                                   standardPath->current_inode);

                        free(pathname);
                }
        }else if (msg_rcv.num == YFS_READ) {
                struct message_file * msg = (struct message_file *) &msg_rcv;
                return_value = ServerRead(msg->inodenum, msg->buf, msg->size, msg->offset, pid);
        } else if (msg_rcv.num == YFS_WRITE) {
                struct message_file * msg = (struct message_file *) &msg_rcv;
                return_value = ServerWrite(msg->inodenum, msg->buf, msg->size, msg->offset, pid);
        } else if (msg_rcv.num == YFS_SEEK) {
                struct message_seek * msg = (struct message_seek *) &msg_rcv;
                return_value = ServerSeek(msg->inodenum, msg->offset, msg->whence, msg->current_position);
        } else if (msg_rcv.num == YFS_LINK) {
                struct message_link * msg = (struct message_link *) &msg_rcv;
                char *oldname = getPathFromProcess(pid, msg->old_name);
                char *newname = getPathFromProcess(pid, msg->new_name);
                return_value = ServerLink(oldname, newname, msg->current_inode);
                free(oldname);
                free(newname);
        } else if (msg_rcv.num == YFS_SYMLINK) {
                return_value = ERROR;
        } else if (msg_rcv.num == YFS_READLINK) {
                return_value = ERROR;
        } else if (msg_rcv.num == YFS_STAT) {
                struct message_stat * msg = (struct message_stat *) &msg_rcv;
                char *pathname = getPathFromProcess(pid, msg->pathname);
                return_value = ServerStat(pathname, msg->current_inode, msg->statbuf, pid);
                free(pathname);
        } else if (msg_rcv.num == YFS_SYNC) {
                return_value = ServerSync();
        } else if (msg_rcv.num == YFS_SHUTDOWN) {
                return_value = ServerShutdown();
        } else {
                TracePrintf(1, "unknown operation %d\n", msg_rcv.num);
                return_value = ERROR;
        }

        // send reply
        struct message_generic msg_rply;
        msg_rply.num = return_value;
        if (Reply(&msg_rply, pid) != 0) {
                TracePrintf(1, "error sending reply to pid %d\n", pid);
        }
}

static char *
getPathFromProcess(int pid, char *pathname)
{
        char *local_pathname = malloc(strlen(pathname) * sizeof (char));
        if (local_pathname == NULL) {
                return NULL;
        }
        if (CopyFrom(pid, local_pathname, pathname,strlen(pathname)) != 0) {
                return NULL;
        }
        return local_pathname;
}