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

void explore_inode(int local_inode_index, int inode_table_block, int inode_number);
int getByteOffset(int block_number); 
int getGroupDescOffset(int group, int table_offset, int desc_size);
int blockIsInUse(char * block_bitmap, int block_id); 
void explore_blocks(struct ext2_group_desc * group_desc, int blocks_per_group, char * block_bitmap); 
int compareBlockId(const void * first, const void * second); 
struct inode_node * mergeSort(struct inode_node * head, int size); 
void explore_inodes(struct ext2_group_desc * group_desc, int block_group, int first_inode); 

struct inode_node {
  struct ext2_inode data;
  struct inode_node * next;
};

static struct ext2_super_block super_block;
static struct inode_node * RECOVERY_CANDIDATES = NULL;
static int NUM_CANDIDATES = 0;
static int fd;

int main (int argc, char* argv[]) {
  int i, blocks_per_group, block_groups;
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
  blocks_per_group = super_block.s_blocks_per_group;
  
  if (super_block.s_blocks_count <= blocks_per_group) {
    block_groups = 1;
    blocks_per_group = super_block.s_blocks_count; 
  } else {
    block_groups = super_block.s_blocks_count / blocks_per_group; 
  }
/*
  printf("Revision Number: %d\n", super_block.s_rev_level);
  printf("Block size: %d\n", block_size);
  printf("Starting at block: %d\n", super_block.s_first_data_block);
  printf("Number of blocks: %d\n", num_blocks);
  printf("Blocks per group: %d\n", blocks_per_group);
  printf("Number of block groups: %d\n", block_groups);
  printf("The first data block (ie the one containing the superblock is block %d\n", super_block.s_first_data_block);  
  printf("The size of the superblock is %d or %d kb \n", SUPERBLOCK_SIZE, SUPERBLOCK_SIZE / 1024);
  printf("The size of a group description is %d bytes\n", (int) sizeof(group_description));  
  printf("Number of inodes per group: %d\n", super_block.s_inodes_per_group);
  printf("\n");
 */ 

  // We'll read all of the block bitmaps into memory into this single array.
  char * block_bitmap = (char *) malloc((blocks_per_group * block_groups) / 8);
  int current_block_bitmap_offset = 0;

  // Flag for whether to take the s_first_ino into account.
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
    printf("\n"); */

    // The first few inodes are not available for standard files, so we probably
    // shouldn't bother looking at them.
    int first_inode_index = 0;
    if (first_block_flag) {
      first_inode_index = super_block.s_first_ino;
      first_block_flag = 0;
    }

    // Explore all inodes in this block group.
    explore_inodes(&group_description, i, first_inode_index);
    explore_blocks(&group_description, blocks_per_group, &block_bitmap[current_block_bitmap_offset]);
    current_block_bitmap_offset += blocks_per_group / 8;
  }
  
  // Sort the candidate list with merge sort by comparing dtines (larger
  // dtime = earlier in the list).
  RECOVERY_CANDIDATES = mergeSort(RECOVERY_CANDIDATES, NUM_CANDIDATES);
  
  int size = 0;
  int capacity = 100;
  int * block_array = (int *) malloc(sizeof(int) * capacity);
  assert(block_array != NULL);
  struct inode_node * current = RECOVERY_CANDIDATES;
 
  while (current != NULL) {
    int initial_size = size;
    
    // Flag indicating whether to recover this file. Default to yes.
    int recover = 1;

    // Do a quicksort on the array.
    qsort((void *)block_array, initial_size, sizeof(int), compareBlockId);

    int explore_indirect_block = 1;
    // Loop over each block pointed to by this inode. Look up the block number
    // in block_array, if present we won't recover this file. Also look up the
    // block number in the block bitmap -- if not free, we won't recover. Add
    // new block numbers to the block array (thus claiming them for this file's
    // exclusive use).
    for (i = 0; i < 12; i++) {
      if (current->data.i_block[i] == 0) {
        explore_indirect_block = 0;
        break;
      }
      if (bsearch((void *) &(current->data.i_block[i]), block_array, initial_size, sizeof(int), compareBlockId) != NULL) {
        // This block has already been reclaimed by a file deleted later than this
        // one. We won't end up recovering this file.
        recover = 0;
      } else {
        // Will add this block to the array. May need to increase the array's
        // capacity.
        if (size == capacity) {
          capacity = capacity * 2;
          block_array = (int *) realloc(block_array, sizeof(int) * capacity);
          assert(block_array != NULL);  // Not enough memory available to double
                                        // the size of the array.
                                        // That would be a pretty major issue
                                        // for this implementation, so let's
                                        // just fail.
        }
        block_array[size] = current->data.i_block[i];
        size++;

        // Check whether this block is marked 'free'.
        if (blockIsInUse(block_bitmap, current->data.i_block[i])) {
          // This block has already been reclaimed, so we should not recover
          // the file.
          recover = 0;
        }
      }
    }

    if (explore_indirect_block) {
      lseek(fd, getByteOffset(current->data.i_block[12]), SEEK_SET);
      int32_t * first_indirect_block = (int32_t *) malloc(block_size);
      assert(first_indirect_block != NULL);
      assert(read(fd, first_indirect_block, block_size) == block_size);
      for (i = 0; i < block_size / sizeof(int32_t); i++) {
        if (first_indirect_block[i] == 0) {
          break;
        }
      } // TODO: do the searching thing above, then look in the second and the third indirect blocks.
    }

    if (recover) {
      // Recover this candidate.
      printf("We are going to recover file with inode #%d\n", current->data.i_ctime);
     
      // Buffer to hold data blocks.
      char * the_block;

      // Open and create the file with proper name.
      // TODO: set the path correctly.
      char path[30];
      sprintf(path, "./recoveredFiles/file-%04d", current->data.i_ctime);
      int recoveredFile = open(path, O_RDWR | O_CREAT); 

      int remaining_bytes = current->data.i_size;
      printf("remaining bytes %d\n", remaining_bytes);
      // Write all the data blocks of the file to the file.
      for (i = 0; i < EXT2_N_BLOCKS; i++) {
        if (current->data.i_block[i] == 0) {
          // TODO: handle indirect blocks.
          break;
        }
        
        int size_to_write;
        if (block_size >= remaining_bytes) {
          size_to_write = remaining_bytes;
        } else {
          size_to_write = block_size;
        }

        lseek(fd, getByteOffset(current->data.i_block[i]), SEEK_SET);
        printf("attempting to read %d bytes\n", size_to_write);
        the_block = (char *) malloc(size_to_write);
        assert(the_block != NULL);
        read(fd, the_block, size_to_write);
        remaining_bytes -= write(recoveredFile, the_block, size_to_write);
        free(the_block);
      } 
      
      // TODO: Set the modified and accessed times.
      
      // Close the file.
      assert(close(recoveredFile) == 0);
    }
    
    // Free this candidate and continue to the next.
    struct inode_node * temp = current;
    current = current->next;
    free(temp);
  }

  // Free the block array.
  free(block_array);

  // Free the block bitmap.
  free(block_bitmap);

  assert(close(fd) == 0);
  return 0;
}

int blockIsInUse(char * block_bitmap, int block_id) {
  int index = block_id - super_block.s_first_data_block;
  char byte = block_bitmap[index / 8];
  int bit = (byte >> index % 8) & 1; 
  return bit;
} 

int compareBlockId(const void * first, const void * second) {
  int a = *((int *) first);
  int b = *((int *) second);
  return a - b;
}

struct inode_node * mergeSort(struct inode_node * head, int size) {
  if (size <= 1) {
    return head;
  }

  // Split the list into halfs.
  int i; 
  int bound = size / 2;
  if (size % 2 == 0) {
    bound--;
  }
  struct inode_node * first = head;
  struct inode_node * second;
  struct inode_node * cur = head;
  for (i = 0; i < bound; i++) {
    cur = cur->next;
  }
  second = cur->next;
  cur->next = NULL;

  // Sort each half.
  first = mergeSort(first, size / 2 + size % 2);
  second = mergeSort(second, size / 2);
  
  // Merge.
  struct inode_node * result;
  if (first->data.i_dtime >= second->data.i_dtime) {
    result = first;
    first = first->next;
  } else {
    result = second;
    second = second->next;
  }
   
  struct inode_node * current = result;
  while (first != NULL && second != NULL) {
    if (first->data.i_dtime >= second->data.i_dtime) {
      current->next = first;  
      first = first->next;
    } else {
      current->next = second;
      second = second->next;
    }
    current = current->next;
  }

  if (first != NULL) {
    current->next = first;
  } else {
    current->next = second;
  }
  return result;
}

int getGroupDescOffset(int group, int table_offset, int desc_size) {
  return table_offset + (group * desc_size);  
}

void explore_blocks(struct ext2_group_desc * group_desc, int blocks_per_group, char * block_bitmap) {
  lseek(fd, getByteOffset(group_desc->bg_block_bitmap), SEEK_SET);
  assert(read(fd, block_bitmap, blocks_per_group / 8) == blocks_per_group / 8);
}

void explore_inodes(struct ext2_group_desc * group_desc, int block_group, int
  first_inode) {
  int i;
  
  // Seek to the location of the inode bit map.
  lseek(fd, getByteOffset(group_desc->bg_inode_bitmap), SEEK_SET);

  // Allocated space for the bit map on the heap. This is one char per every 8
  // inodes, since each inode will be represented with a single bit.
  char * bitmap = (char *) malloc(super_block.s_inodes_per_group / 8);
  assert(bitmap != NULL);
  
  // Copy the bitmap into main memory.
  assert(read(fd, bitmap, super_block.s_inodes_per_group / 8) == super_block.s_inodes_per_group / 8);
  // Loop over each bit in the bitmap, if 0 we have a 'free' inode to examine. 
  for (i = first_inode; i < super_block.s_inodes_per_group; i++) {
    char byte = bitmap[i / 8];
    int bit = (byte >> i % 8) & 1; 
    if (bit == 0) {
      // This is a 'free' inode which may need to be undeleted.

      // Inodes start at inode 1. The inode number of this inode then must be 1
      // plus the number of inodes in previous blocks plus the current index in
      // this bitmap.
      int inode_number = 1 + (block_group * super_block.s_inodes_per_group) + i;
      explore_inode(i, group_desc->bg_inode_table, inode_number);  
   }
  }
  free(bitmap);
}

void explore_inode(int local_inode_index, int inode_table_block, int inode_number) {
  struct inode_node * inode; 

  // Need to grab the inode. First seek to the apporpriate byte offset.
  int inode_byte_offset = super_block.s_inode_size * local_inode_index;
  int byte = getByteOffset(inode_table_block) + inode_byte_offset;
  lseek(fd, byte, SEEK_SET);  

  // Read the inode.
  inode = (struct inode_node *) malloc(sizeof(struct inode_node));
  read(fd, inode, sizeof(struct ext2_inode));

  int32_t file_size = inode->data.i_size;
  //long long upper_bits = inode->data.i_dir_acl;
  //long long file_size = inode->data.i_size + (upper_bits << 32);
  if (file_size > 0) {
    /*if (upper_bits != 0) {
    printf("upper 32 bits %d\n", inode->data.i_dir_acl);
    printf("lower 32 bits %d\n", inode->data.i_size);
    }*/

    //printf("file size %lld\n", file_size);
    
    // This is a candidate file which may have been deleted. 

    // First use the i_ctime field to store the indoe_number. We're not going to
    // write this to disk, but we want to keep the inode number associated with the
    // inode in case we end up recovering this file later.
    inode->data.i_ctime = inode_number;

    // Now we'll add this inode to the list of candidates.
    inode->next = RECOVERY_CANDIDATES; 
    RECOVERY_CANDIDATES = inode;
    NUM_CANDIDATES++;

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
      // lseek(fd, getByteOffset(inode.i_block[i], super_block.s_first_data_block, block_size), SEEK_SET);
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

/* Returns the byte offset for the given block number */
int getByteOffset(int block_number) {
  return SUPERBLOCK_OFFSET + (super_block.s_log_block_size * (block_number - super_block.s_first_data_block));
}
