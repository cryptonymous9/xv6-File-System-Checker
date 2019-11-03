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


//struct dinode inode;
/*This checks for corrupted i node */
int corrupted_inode()        //This code works NO need to mess with it.
{
    struct dinode inode;
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    for (int i=0;i<(int)sb.ninodes;i++)
    {
        lseek(fsfd,sb.inodestart*BSIZE+i*sizeof(struct dinode),SEEK_SET);
        read(fsfd,buf,sizeof(inode));
        memmove(&inode, buf, sizeof(inode));
        if(inode.type!=0 && inode.type!=T_FILE && inode.type!=T_DIR && inode.type!=T_DEV)
        {
            printf("ERROR: bad inode.");
            close(fsfd);
            return 1;
        }

        //This checks for error 12
        //No extra links allowed for directories, each directory only appears in one other directory
        if(inode.type == T_DIR && inode.nlink > 1)
        {
            printf("ERROR: directory appears more than once in file system.");
            close(fsfd);
            return 1;
        }
    }
    return 0;
}
/*Function to find directory inode by name*/
int find_directory_by_name(uint addr, char *name)
{
    struct dinode inode;
    struct dirent buf;
    lseek(fsfd,addr*BSIZE,SEEK_SET);
    //read(fsfd,&buf,sizeof(struct dirent));
    for(int i=0;i<BSIZE/sizeof(struct dirent);i++)
    {
        read(fsfd,&buf,sizeof(struct dirent));
        if(buf.inum==0)
            continue;
        if(strncmp(name,buf.name,DIRSIZ)==0)
        {
            return buf.inum;
        }
    }
    return -1;
}

/*Error 7 each DAddress must be used only once.*/
int check_address(uint* address, struct dinode inode)
{  

    for(int i=0;i<NDIRECT+1;i++){

        if(inode.addrs[i] == 0) {continue;}                              
        
        if(address[inode.addrs[i]] == 1) {
            printf("ERROR: address used more than once\n");
            return 1;}     
        address[inode.addrs[i] ]=1;                                 
    }
    
    int j;
    uint addr;
    if(inode.addrs[NDIRECT] != 0){
        for(j=0; j<NINDIRECT; j++){
            if (lseek(fsfd, inode.addrs[NDIRECT] * BSIZE + j*sizeof(uint), SEEK_SET) != inode.addrs[NDIRECT] * BSIZE + j*sizeof(uint)){
                perror("lseek");
            }
            if (read(fsfd, &addr, sizeof(uint)) != sizeof(uint)){
                perror("read");
            }
            if(addr==0) {continue;}
            
            if(address[addr] == 1) {return 1;}
            address[addr]=1;
        }
    }
    return 0;
}

/*error 5*/
int check_inode_addr(struct dinode inode)
{
    uint buf;
    uint abuf;
    int offset;
    for(int j=0;j<NDIRECT;j++)
    {
        lseek(fsfd,sb.bmapstart+inode.addrs[j]/8,SEEK_SET);
        read(fsfd,&buf,1);
        offset=inode.addrs[j]%8;
        buf=(buf>>offset)%2;
        if(buf==0)
        {
            printf("ERROR: address used by inode but marked free in bitmap.");
            return 1;
        }
    }
    if(inode.addrs[NDIRECT]!=0)
    {
        lseek(fsfd,inode.addrs[NDIRECT]*BSIZE,SEEK_SET);
        for(int i=0;i<NINDIRECT;i++)
        {
            read(fsfd,&abuf,sizeof(uint));
            offset=abuf%8;
            lseek(fsfd,sb.bmapstart+abuf/8,SEEK_SET);
            read(fsfd,&buf,1);
            buf=(buf>>offset)%2;
            if(buf==0)
            {
                printf("ERROR: address used by inode but marked free in bitmap.");
                return 1;
            }
        }
    }
}


/*ERROR 4: Check the directories for errors*/
int check_directory(uint *address)
{
    struct dinode inode;
    int dot_inode=-1;
    int ddot_inode=-1;
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    for (int i=0;i<sb.ninodes;i++)
    {
        if (check_address(address, inode))
        {
            return 1;
        }
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
                    printf("ERROR 1: directory not properly formatted.\n");
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
                        printf("ERROR 2: directory not properly formatted.\n");
                        return 1;
                    }
                }
                else
                {
                    printf("ERROR 3: directory not properly formatted.\n");
                    printf("%d %d\n", dot_inode, ddot_inode);
                    return 1;
                }
            }
            if(dot_inode!=i || dot_inode==-1 || ddot_inode==-1)
            {
                printf("ERROR 4: directory not properly formatted.\n");
                return 1;
            }
        }
    }
    return 0;
}
int check_root()
{
    struct dinode inode;
    char buf[sizeof(struct dinode)];
    lseek(fsfd,sb.inodestart*BSIZE+sizeof(struct dinode),SEEK_SET);
    read(fsfd,buf,sizeof(struct dinode));
    memmove(&inode,buf,sizeof(inode));
    //printf("%d",inode.type);
    if(inode.type!=1)
    {
        printf("ERROR: root directory does not exist.\n");
        close(fsfd);
        return 1;
    }
    /*else
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
                printf("ERROR: root directory does not exist.\n");
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
                printf("ERROR: root directory does not exist.\n");
                return 1;
            }
        }
        else
        {
            printf("ERROR: root directory does not exist.\n");
            return 1;
        }
    }*/
    return 0;
}



int check_block_inuse(uint* address){

    // Position od datablock in Bitmap
    // Bitmapstart(in Bytes) + number of bytes in Bitmap(# total blocks%8) - number of bytes of dataBlocks(# dataBlocks%8) 
    int db_inbmap =sb.bmapstart*BSIZE + sb.size/8 - sb.nblocks/8;
    
    // Current address
    int current_block=(sb.bmapstart + 1);

    // Seeking cursor to the first byte of DataBlock in BitMap
    lseek(fsfd, db_inbmap, SEEK_SET);

    uint bit_to_check; 
    int byte_to_check;
    
    // taking Bytewise addresses from BitMap
    for (int i=current_block; i<sb.size; i+=8){

        // reading 1 Byte => it will contain usage info. about 8 DataBlocks
        read(fsfd, &byte_to_check, 1);
        for (int x=0; x<8; x++){

            //  Reading last bit step-by-step each time in the corresponding Byte   
            bit_to_check = (byte_to_check >> x)%2;

            // bit !=0 => when DataBlock marked as in-use in BitMap
            if (bit_to_check!=0){
                // address[current_block] is 0 when it is not in use
                if(address[current_block]==0){
                    printf("ERROR: bitmap marks block in use but it is not in use.");
                    return 1;
                }
            }
            current_block++;
        }
    } 
    return 0;
}





int main(int argc, char *argv[])
{   

    
    uint address[sb.size];
    // initializing array address with zeores
    // For error involving blocks-in-use
    for(int i=0;i<sb.size;i++) {
        address[i]=0;
    }

    if(argc<2)
    {
        printf("usage $fsck file_system_image.img\n");
        exit(1);
    }
    fsfd = open(argv[1],O_RDONLY);
    lseek(fsfd,BSIZE,SEEK_SET);
    uchar buf[BSIZE];
    read(fsfd,buf,BSIZE);
    memmove(&sb,buf,sizeof(sb));
    //error 1 
    if(corrupted_inode()==1)
        return 1;
    //error3 starts
    if(check_root()==1)
        return 1;
    //error3 ends

    if(check_directory(address)==1){
        return 1;
    }

    // error6 starts
    if (check_block_inuse(address)){
        return 1;
    }
    // error6 ends
    printf("Your File System is intact\n");
    return 0;
}
