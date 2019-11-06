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
            printf("ERROR: bad inode.\n");
            close(fsfd);
            return 1;
        }
        
        //This checks for error 11
        //Refrence counts (number of links) for regular files match the number of times file is referred to in directories (i.e., hard links work correctly)
        //ERROR: bad refrence count for file
        // if(inode.type == T_FILE)
        // {
        //     if(inode.nlink != check_links(inode, i))
        //     {
        //         printf("ERROR: bad reference count for file.");
        //         close(fsfd);
        //         return 1;
        //     }
        // }

        //This checks for error 12
        //No extra links allowed for directories, each directory only appears in one other directory
        if(inode.type == T_DIR && inode.nlink > 1)
        {
            printf("ERROR: directory appears more than once in file system.\n");
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

/*Error 7/8 each address must be used only once. Error 2 bad direct or indirect address*/
int check_address(uint* address, struct dinode inode)
{ 
    int dstart=sb.bmapstart+1; 
    for(int i=0;i<NDIRECT;i++)
    {

        if(inode.addrs[i] == 0) {continue;}                              
        if(inode.addrs[i]<dstart || inode.addrs[i]>=dstart+sb.nblocks)
        {
            printf("ERROR: bad direct address in inode.\n");
            return 1;
        }
        if(address[inode.addrs[i]] == 1)
        {
            printf("ERROR: direct address used more than once\n");
            return 1;
        }     
        address[inode.addrs[i]]=1;                                 
    }
    
    uint addr;
    if(inode.addrs[NDIRECT] != 0)
    {
        for(int j=0; j<NINDIRECT; j++)
        {
            lseek(fsfd, inode.addrs[NDIRECT] * BSIZE + j*sizeof(uint), SEEK_SET);
            read(fsfd, &addr, sizeof(uint));

            if(addr==0) {continue;}
            if(addr<dstart || addr>=dstart+sb.nblocks)
            {
                printf("ERROR: bad indirect address in inode.\n");
                return 1;
            }
            if(address[addr] == 1)
            {
                printf("ERROR: indirect address used more than once\n");
                return 1;
            }
            address[addr]=1;
        }
    }
    return 0;
}

int check_inode_ref(struct dinode inode)
{
    struct dirent dir_entry;
    struct dinode ref_node;
    for (int i=0;i<NDIRECT;i++)
    {
        if(inode.addrs[i]==0)
            continue;
        for(int j=0;j<BSIZE/sizeof(struct dirent);j++)
        {
            lseek(fsfd,inode.addrs[i]*BSIZE+j*sizeof(struct dirent),SEEK_SET);
            read(fsfd,&dir_entry,sizeof(struct dirent));
            if(dir_entry.inum==0)
            {
                continue;
            }
            lseek(fsfd,sb.inodestart*BSIZE+dir_entry.inum*sizeof(struct dinode),SEEK_SET);
            read(fsfd,&ref_node,sizeof(struct dinode));
            if(ref_node.type==0)
            {
                printf("ERROR: inode referred to in directory but marked free.\n");
                return 1;
            }
        }
    }
    if(inode.addrs[NDIRECT]!=0)
    {
        lseek(fsfd,inode.addrs[NDIRECT]*BSIZE,SEEK_SET);
        for (int k=0;k<BSIZE/sizeof(struct dirent);k++)
        {
            lseek(fsfd,inode.addrs[NDIRECT]*BSIZE+k*sizeof(struct dirent),SEEK_SET);
            read(fsfd,&dir_entry,sizeof(struct dirent));
            if(dir_entry.inum==0)
            {
                continue;
            }
            lseek(fsfd,sb.inodestart*BSIZE+dir_entry.inum*sizeof(struct dinode),SEEK_SET);
            read(fsfd,&ref_node,sizeof(struct dinode));
            if(ref_node.type==0)
            {
                printf("ERROR: inode referred to in directory but marked free.\n");
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
    //lseek(fsfd,sb.inodestart*BSIZE,SEEK_SET);
    for (int i=0;i<sb.ninodes;i++)
    {
        lseek(fsfd, sb.inodestart*BSIZE + i*sizeof(struct dinode), SEEK_SET);
        read(fsfd,buf,sizeof(struct dinode));
        memmove(&inode, buf, sizeof(struct dinode));
        if (check_address(address, inode))
        {
            return 1;
        }
        if (check_inode_ref(inode))
        {
            //printf("printing\n");
            return 1;
        }
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
                if (dot_inode!=-1 && dot_inode!=i+1)
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
                    if(dot_inode!=i+1 || dot_inode==-1 || ddot_inode==-1)
                    {
                        printf("ERROR 2: directory not properly formatted.\n");
                        return 1;
                    }
                }
                else
                {
                    printf("ERROR 3: directory not properly formatted.\n");
                    // printf("%d %d %d\n", dot_inode, ddot_inode, inode.addrs[NDIRECT]);
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

/*
ERROR 
*/
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
                    printf("ERROR: bitmap marks block in use but it is not in use.\n");
                    return 1;
                }
            }
            current_block++;
        }
    } 
    return 0;
}

/*ERROR 5:
Checks if for in-use inodes, each address in use is also marked in use in the bitmap.

Returns: 'ERROR: address used by inode but marked free in bitmap.' if error exists
*/
int check_inode_addr(struct dinode inode)
{
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
            if (address[current_block]!=0){
                // address[current_block] is 0 when it is not in use
                if(bit_to_check==5){
                    printf("ERROR: bitmap marks block in use but it is not in use.\n");
                    return 1;
                }
            }
            current_block++;
        }
    } 
    return 0;
}


/* 
ERROR 11: 
Reference counts (number of links) for regular files match the number of times file is referred to in directories

Consists of two functions -
1. traverse_dir_by_inum(): helper to iterate over directory based on inum
2. check_links(): checks all links from an inode
*/

// Error 11 (Helper)
int traverse_dir_by_inum(uint addr, ushort inum)
{
    lseek(fsfd, addr*BSIZE, SEEK_SET)
    struct dirent buf;
    int i;
    for(int i=0;i<BSIZE/sizeof(struct dirent);i++)
    {
        read(fsfd,&buf,sizeof(struct dirent));
        if(buf.inum==inum)
        {
            return 0;
        }            
    }
    return 1;
}

//Error 11 (Main)
int check_links(struct dinode current_inode, uint current_inum)
{
    int inum;
    int count = 0;
    
    struct dinode in;
    for(inum = 0; inum < sb.ninodes; inum++)
    {
        if(inum == current_inum && inum != 1)
        {
            continue;
        }
        if(lseek(fsfd, sb.inodestart * BSIZE + inum * sizeof(struct dinode), SEEK_SET) != sb.inodestart * BSIZE + inum * sizeof(struct dinode))
        {
            perror("lseek");
            exit(1);
        }
        if(read(fsfd, &in, sizeof(struct dinode)) != sizeof(struct dinode))
        {
            perror("read");
            exit(1);
        }
        if(in.type != T_DIR)
        {
            continue;
        }

        int x;
        for(x = 0; x <NDIRECT; x++)
        {
            if(in.addrs[x] == 0)
            {
                continue;    
            }
            if(0 == traverse_dir_by_inum(in.addrs[x], current_inum))
            {
                count++;
            }
        }

        int y;
        uint directory_address;
        if(in.addrs[NDIRECT] != 0)
        {
            for(y = 0; y <NINDIRECT; y++)
            {
                if(lseek (fsfd, in.addrs[NDIRECT] * BSIZE + y*sizeof(uint), SEEK_SET) != in.addrs[NDIRECT] * BSIZE + y* sizeof(uint))
                {
                    perror("lssek");
                    exit(1);
                }
                if(read(fsfd, &directory_address, sizeof(uint)) != sizeof(uint))
                {
                    perror("read");
                    exit(1);
                }
                if(0 == directory_address)
                {
                    continue;
                }
                if(0 == traverse_dir_by_inum(directory_address, current_inum))
                {
                    count++;
                }
            }
        }
    }
    return count;
}


/* ERROR 9:
For all inodes marked in use, each must be referred to in at least one directory. 

Returns: 'ERROR: inode marked use but not found in a directory.' if error found
*/

// Error 9 (Helper)
int check_inum_indir(uint addr, ushort inum){   
    lseek(fsfd, addr*BSIZE, SEEK_SET);
    struct dirent buf;
    for(int i=0; i < BSIZE / sizeof(struct dirent); i++){
        read(fsfd, &buf, sizeof(struct dirent));
        if(buf.inum==inum){
            return 0;
        }
    }
    return 1;
}   

// Error 9 (Main)
int inode_check_directory(uint target_inum){

    // DIR-inode to compare
    struct dinode compare_inode;

    // loop over all the DIR inodes to find the reference to the in-use target inode
    for (int compare_inum=0; compare_inum<sb.ninodes; compare_inum++){                                                                          

        // storing the current inode for comparison                                                                       
        lseek(fsfd, sb.inodestart*BSIZE + compare_inum * sizeof(struct dinode), SEEK_SET);
        read(fsfd, &compare_inode, sizeof(struct dinode));
        
        // 1-> T_DIR
        // skipping if it is not a Directory
        if (compare_inode.type!=1) {continue;}                                                                              
        
        // looping through all the direct-pointers
        for(int d_ptr=0; d_ptr<NDIRECT; d_ptr++){
            
            // skipping if it is empty
            if(compare_inode.addrs[d_ptr]==0) {continue;}

            //checking if in-use target inode number present in the compare DIR inode
            else if(check_inum_indir(compare_inode.addrs[d_ptr], target_inum)==0){return 0;}  
            
        }
        
        // For all the indirect addresses
        uint ind_DIR_address;

        // looping through all the indirect-pointers
        
        for(int ind_ptr=0; ind_ptr<NINDIRECT; ind_ptr++){
            
            lseek(fsfd, compare_inode.addrs[NDIRECT] * BSIZE + ind_ptr*sizeof(uint), SEEK_SET);
            read(fsfd, &ind_DIR_address, sizeof(uint));

            // skipping if empty
            if(ind_DIR_address==0) {continue;}
            // checking if in-use target inode number present in the indirect address
            if(check_inum_indir(ind_DIR_address, target_inum)==0) {return 0;}
        }
    }
    // in-use inode not found in any directory.
    printf("ERROR: inode marked use but not found in a directory\n");
    return 1;
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

    // if(check_directory(address)==1){
    //     return 1;
    // }

    // error6 starts
    if (check_block_inuse(address)){
        return 1;
    }
    // error6 ends

    //error 9
    struct dinode current_inode;

    // looping through all inodes to check all the in-use inodes
    for (int current_inum = 0; current_inum < ((int) sb.ninodes); current_inum++){
        lseek(fsfd, sb.inodestart * BSIZE + current_inum * sizeof(struct dinode), SEEK_SET); 
        read(fsfd, buf, sizeof(struct dinode))!=sizeof(struct dinode);
        memmove(&current_inode, buf, sizeof(current_inode));

        // only checking if the inode is in use
        if (current_inode.type!=0){
            if (inode_check_directory(current_inum)){
                return 1;
            }
        }    
    }

    if(check_directory(address)==1){
        return 1;
    }


    printf("Your File System is intact\n");
    return 0;
}
