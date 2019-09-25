#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define stat xv6_custom_stat  // avoid clash with host (struct stat in stat.h)
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
        read(fsfd,buf,sizeof(inode));
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

int find_directory_by_name(uint addr, char *name)
{
    struct dirent buf;
    lseek(fsfd,addr*BSIZE,SEEK_SET);
    read(fsfd,&buf,sizeof(struct dirent));
    if(strncmp(name,buf.name,DIRSIZ)==0)
    {
        return buf.inum;
    }
    return -1;
}

int check_directory()
{
    int dot_inode=-1;
    int ddot_inode=-1;
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    for (int i=0;i<sb.ninodes;i++)
    {
        read(fsfd,buf,sizeof(inode));
        memmove(&inode, buf, sizeof(inode));
        if(inode.type==T_DIR)
        {
            for(int j=0;j<NDIRECT;j++)
            {
                if(inode.addrs[j]==0)
                    continue;
                if(dot_inode==-1)
                    dot_inode=find_directory_by_name(inode.addrs[j],".");
                if(ddot_inode==-1)
                    ddot_inode=find_directory_by_name(inode.addrs[j],"..");
            }
            if(dot_inode==-1 || ddot_inode==-1)
            {
                if (dot_inode!=-1 && dot_inode!=i)
                {
                    fprintf(stderr,"ERROR: directory not properly formatted.\n");
                    return 1;
                }
                else if(inode.addrs[NDIRECT]!=0)
                {
                    lseek(fsfd,inode.addrs[NDIRECT]*BSIZE,SEEK_SET);
                    uint indbuf;
                    for(int k=0;k<NINDIRECT;k++)
                    {
                        read(fsfd,&indbuf,sizeof(uint));
                        if(dot_inode==-1)
                            dot_inode=find_directory_by_name(indbuf,".");
                        if(ddot_inode==-1)
                            ddot_inode=find_directory_by_name(indbuf,"..");
                        if(dot_inode!=-1 && ddot_inode!=-1)
                            break;
                    }
                    if(dot_inode!=i || dot_inode==-1 || ddot_inode==-1)
                    {
                        fprintf(stderr,"ERROR: directory not properly formatted.\n");
                        return 1;
                    }
                }
                else
                {
                    fprintf(stderr,"ERROR: directory not properly formatted.\n");
                    return 1;
                }
            }
            if(dot_inode!=i || dot_inode==-1 || ddot_inode==-1)
            {
                fprintf(stderr,"ERROR: directory not properly formatted.\n");
                return 1;
            }
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
    if(inode.type!=T_DIR)
    {
        fprintf(stderr,"ERROR: root directory does not exist.\n");
        close(fsfd);
        return 1;
    }
    else
    {
        int dot_inode=-1;
        int ddot_inode=-1;
    //char buf[sizeof(struct dinode)];
        for(int j=0;j<NDIRECT;j++)
        {
            if(inode.addrs[j]==0)
                continue;
            if(dot_inode==-1)
                dot_inode=find_directory_by_name(inode.addrs[j],".");
            if(ddot_inode==-1)
                ddot_inode=find_directory_by_name(inode.addrs[j],"..");
        }
        if(dot_inode!=-1 && ddot_inode!=-1)
        {
            if(dot_inode!=1 && ddot_inode!=1)
            {
                fprintf(stderr,"ERROR: root directory does not exist.\n");
                return 1;
            }
        }
        else if(inode.addrs[NDIRECT]!=0)
        {
            lseek(fsfd,inode.addrs[NDIRECT]*BSIZE,SEEK_SET);
            uint indbuf;
            for(int k=0;k<NINDIRECT;k++)
            {
                read(fsfd,&indbuf,sizeof(uint));
                if(dot_inode==-1)
                    dot_inode=find_directory_by_name(indbuf,".");
                if(ddot_inode==-1)
                    ddot_inode=find_directory_by_name(indbuf,"..");
                if(dot_inode!=-1 && ddot_inode!=-1)
                    break;
            }
            if(dot_inode!=1 || dot_inode==-1 || ddot_inode==-1 || ddot_inode!=1)
            {
                fprintf(stderr,"ERROR: root directory does not exist.\n");
                return 1;
            }
        }
        else
        {
            fprintf(stderr,"ERROR: root directory does not exist.\n");
            return 1;
        }
    }
    return 0;
}

int check_address(uint* addresses){
    for(int i=0;i<NDIRECT+1;i++){                                        
        if(inode.addrs[i] == 0) {continue;}                              
        
        if(addresses[inode.addrs[i]] == 1) {
            fprintf(stderr,"ERROR: address used more than once\n");
            return 1;}     
        addresses[inode.addrs[i] ]=1;                                 
    }
    
    int j;
    uint address;
    if(inode.addrs[NDIRECT] != 0){
        for(j=0; j<NINDIRECT; j++){
            if (lseek(fsfd, inode.addrs[NDIRECT] * BSIZE + j*sizeof(uint), SEEK_SET) != inode.addrs[NDIRECT] * BSIZE + j*sizeof(uint)){
                perror("lseek");
            }
            if (read(fsfd, &address, sizeof(uint)) != sizeof(uint)){
                perror("read");
            }
            if(address==0) {continue;}
            
            if(addresses[address] == 1) {return 1;}
            addresses[address]=1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if(argc<2)
    {
        fprintf(stderr,"usage $fsck file_system_image.img\n");
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
    printf("Your File System is intact\n");
    return 0;
}