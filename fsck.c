#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "types.h"
#include "fs.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"

int fsfd;
struct superblock sb;
struct dinode inode;
int corrupted_inode()
{
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    for (int i=0;i<sb.ninodes;i++)
    {
        memmove(&inode, buf, sizeof(inode));
        if(inode.type!=0 && inode.type!=T_FILE && inode.type!=T_DIR && inode.type!=T_DEV)
        {
            fprintf(stderr,"ERROR: bad inode.");
            close(fsfd);
            return 1;
        }
    }
    return 0;
}
int check_root()
{
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    read(fsfd,buf,sizeof(struct dinode));
    memmove(&inode,buf,sizeof(inode));
    if(inode.type!=1)
    {
        fprintf(stderr,"ERROR: root directory does not exist.");
        close(fsfd);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if(argc<2)
    {
        fprintf(stderr,"usage $fsck file_system_image.img");
        exit(1);
    }
    fsfd = open(argv[1],O_RDONLY);
    //error 1 
    if(corrupted_inode==1)
        return 1;
    //error3 starts
    if(check_root()==1)
        return 1;
    //error3 ends

}