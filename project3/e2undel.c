/* Jenny Abrahamson CSE 451 12/4/2011 Project 3 Undelete */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ext2fs/ext2fs.h> 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


void explore_blocks(int fd, struct ext2_group_desc * group_desc, int block_size, int first_data_block, int block_group, int blocks_per_group);
void explore_inode(int local_inode_index, int inode_table_block, int inode_size, int fd, int block_size, int first_data_block, int inode_number, int active, int inodes_per_group);
void explore_inodes(int fd, struct ext2_group_desc * group_desc, int block_size, int first_data_block, int first_inode, int inode_table_block, int inode_size, int block_group, int inodes_per_group); 
int getBlock(int byte_offset, int first_data_block, int block_size); 
int getGroupDescOffset(int group, int table_offset, int desc_size);
int getByteOffset(int block_number, int first_data_block, int block_size); 
int getBlockNumOfGroupDescTable(int first_data_block, int block_size);

struct inode_node {
  ext2_inode data;
  inode_node * next;
}
static inode_node * RECOVERY_CANDIDATES = NULL;


int main (int argc, char* argv[]) {
  int i, fd, block_size, num_blocks, blocks_per_group, block_groups;
  struct ext2_super_block super_block;
  struct ext2_group_desc group_description;

  // Check that argc > 0, if not print error and exit.
  if (argc < 2) {
    printf("Usage: Provide file system to examine as first parameter\n");
    return -1;
  }

  // Open file system passed as argv[1].
  fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    printf("Unable to open %s. Provide valid Ext2 file system.\n", argv[1]);
    return -1;
  }

  // Now we'll read the superblock to determine locations of block groups:

  // First seek to superblock location, copy superblock struct.
  assert(lseek(fd, SUPERBLOCK_OFFSET, SEEK_SET) == SUPERBLOCK_OFFSET);

  assert(read(fd, &super_block, sizeof(super_block)) == sizeof(super_block));
  block_size = 1024 << super_block.s_log_block_size;
  num_blocks = super_block.s_blocks_count;
  blocks_per_group = super_block.s_blocks_per_group;
  
  if (num_blocks <= blocks_per_group) {
    block_groups = 1;
    blocks_per_group = num_blocks; 
  } else {
    block_groups = num_blocks / blocks_per_group; 
  }
/*
  printf("Block size: %d\n", block_size);
  printf("Number of blocks: %d\n", num_blocks);
  printf("Blocks per group: %d\n", blocks_per_group);
  printf("Number of block groups: %d\n", block_groups);
  printf("The first data block (ie the one containing the superblock is block %d\n", super_block.s_first_data_block);  
  printf("The size of the superblock is %d or %d kb \n", SUPERBLOCK_SIZE, SUPERBLOCK_SIZE / 1024);
  printf("The group description block table begins in block %d\n", getBlockNumOfGroupDescTable(super_block.s_first_data_block, block_size));
  printf("The size of a group description is %d bytes\n", (int) sizeof(group_description));  
  printf("Number of inodes per group: %d\n", super_block.s_inodes_per_group);
  printf("\n");
 */ 
  int first_block_flag = 1;

  // Next visit block group descriptor entry.
  for (i = 0; i < block_groups; i++) {

    // Seek to correct location of the group description. 
    int cur_desc_offset = getGroupDescOffset(i, SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE, sizeof(group_description));
    lseek(fd, cur_desc_offset, SEEK_SET);
   
    // Read the current group description.
    assert(read(fd, &group_description, sizeof(group_description)) == sizeof(group_description));
/*
    printf("the block bitmap is located at %d\n", group_description.bg_block_bitmap);
    printf("the inode bitmap is located at %d\n", group_description.bg_inode_bitmap);
    printf("the inode table is located at %d\n", group_description.bg_inode_table);
    printf("this description is located in block %d\n", getBlock(cur_desc_offset, super_block.s_first_data_block, block_size));
    printf("\n"); */
    int first_inode_index = 0;
    if (first_block_flag) {
      first_inode_index = super_block.s_first_ino;
      first_block_flag = 0;
    }
    explore_inodes(fd, &group_description, block_size, super_block.s_first_data_block, first_inode_index, group_description.bg_inode_table, super_block.s_inode_size, i, super_block.s_inodes_per_group);
   // explore_blocks(fd, &group_description, block_size, super_block.s_first_data_block, i, blocks_per_group);
  }

  assert(close(fd) == 0);
  return 0;
}

int getGroupDescOffset(int group, int table_offset, int desc_size) {
  return table_offset + (group * desc_size);  
}
/*
void explore_blocks(int fd, struct ext2_group_desc * group_desc, int block_size, int first_data_block, int block_group, int blocks_per_group) {
  lseek(fd, getByteOffset(group_desc->bg_block_bitmap, first_data_block, block_size), SEEK_SET);
  int i;
  char * bitmap = (char *) malloc(blocks_per_group / 8);
  assert(bitmap != NULL);
  
  assert(read(fd, bitmap, blocks_per_group / 8) == blocks_per_group / 8);
  for (i = 0; i < blocks_per_group; i++) {
    char byte = bitmap[i / 8];
    int bit = (byte >> i % 8) & 1;
    int block_id = (blocks_per_group * block_group) + i + first_data_block;
    printf("Block with local index %d, id #%d, in group %d is active? --> %d\n", i, block_id, block_group, bit);
  }

  free(bitmap);
}*/

void explore_inodes(int fd, struct ext2_group_desc * group_desc, int block_size, int first_data_block, int first_inode, int inode_table_block, int inode_size, int block_group, int inodes_per_group) {
  int i;
  //printf("Exploring block group %d, which has %d dedicated directory inodes\n", block_group, group_desc->bg_used_dirs_count);
  
  // Seek to the location of the inode bit map.
  lseek(fd, getByteOffset(group_desc->bg_inode_bitmap, first_data_block, block_size), SEEK_SET);

  // Allocated space for the bit map on the heap. This is one char per every 8
  // inodes, since each inode will be represented with a single bit.
  char * bitmap = (char *) malloc(inodes_per_group / 8);
  assert(bitmap != NULL);
  
  // Copy the bitmap into main memory.
  assert(read(fd, bitmap, inodes_per_group / 8) == inodes_per_group / 8);
  // Loop over each bit in the bitmap, if 0 we have a 'free' inode to examine. 
  for (i = first_inode; i < inodes_per_group; i++) {
    char byte = bitmap[i / 8];
    int bit = (byte >> i % 8) & 1; 
    if (bit == 0) {
      // This is a 'free' inode which may need to be undeleted.

      // Inodes start at inode 1. The inode number of this inode then must be 1
      // plus the number of inodes in previous blocks plus the current index in
      // this bitmap.
      int inode_number = 1 + (block_group * inodes_per_group) + i;
      explore_inode(i, inode_table_block, inode_size, fd, block_size, first_data_block, inode_number, bit, inodes_per_group);  
   }
  }
  free(bitmap);
}
void explore_inode(int local_inode_index, int inode_table_block, int inode_size, int fd, int block_size, int first_data_block, int inode_number, int active, int inodes_per_group) {
  struct inode_node * inode; 
  int i;

  // Need to grab the inode. First seek to the apporpriate byte offset.
  int inode_byte_offset = inode_size * local_inode_index;
  int byte = getByteOffset(inode_table_block, first_data_block, block_size) + inode_byte_offset;
  lseek(fd, byte, SEEK_SET);  

  char * the_block; 
  // Read the inode.
  inode = (struct inode_node *) malloc(sizeof(struct inode_node));
  read(fd, inode, sizeof(struct ext2_inode));
  
  if (inode->data.i_size != 0) {
    // This is a candidate file which may have been deleted. 

    // First use the i_ctime field to store the indoe_number. We're not going to
    // write this to disk, but we want to keep the inode number associated with the
    // inode in case we end up recovering this file later.
    inode->data.i_ctime = inode_number;

    // Now we'll add this inode to the list of candidates.
    inode->next = RECOVERY_CANDIDATES; 
    RECOVERY_CANDIDATES = inode;

    // printf("Examining inode #%d which has ", inode_number);
    // printf("Delete time: %d\n", inode.i_dtime);
    // printf("file size: %d and dtime %d \n", inode.i_size, inode.i_dtime);
    // printf("Mode: %x\n", inode.i_mode);
    // printf("Last modified: %d\n", inode.i_mtime);
    // printf("Created: %d\n", inode.i_ctime);
    // printf("Last accessed: %d\n", inode.i_atime);
   
    // for (i = 0; i < EXT2_N_BLOCKS; i++) {
      // printf("%d = %d\n", i, inode.i_block[i]);
      // if (inode.i_block[i] == 0) {
        // break;
      // }
      // Now let's go fetch the block.
      // lseek(fd, getByteOffset(inode.i_block[i], first_data_block, block_size), SEEK_SET);
      // the_block = (char *) malloc(block_size / 8);
      // assert(the_block != NULL);
      // read(fd, the_block, block_size / 8);
      // printf("The block? %s\n", the_block);
      // free(the_block);
      // the_block = NULL;
    // }
  } else {
    // This is not a candidate for recovery.
    free(inode);
  }
}


int getBlockNumOfGroupDescTable(int first_data_block, int block_size) {
  int blocks_for_super_block;
  if (SUPERBLOCK_SIZE <= block_size) {
    blocks_for_super_block = 1;
  } else {
    blocks_for_super_block = SUPERBLOCK_SIZE / block_size;
    if (SUPERBLOCK_SIZE % block_size != 0) {
      blocks_for_super_block++;
    }
  }
  return first_data_block + blocks_for_super_block;
}

int getBlock(int byte_offset, int first_data_block, int block_size) {
  return first_data_block + ((byte_offset - SUPERBLOCK_OFFSET) / block_size); 
}

/* Returns the byte offset for the given block number */
int getByteOffset(int block_number, int first_data_block, int block_size) {
  return SUPERBLOCK_OFFSET + (block_size * (block_number - first_data_block));
}
