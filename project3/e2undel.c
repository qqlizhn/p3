/* Jenny Abrahamson CSE 451 12/4/2011 Project 3 Undelete */

#include <assert.h>
#include <ext2fs/ext2fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static int64_t getFileSize(int32_t lower_bits, int32_t upper_bits);
static void explore_inodes(struct ext2_group_desc * group_desc, int32_t block_group, int32_t
  first_inode);
static int64_t getGroupDescOffset(int32_t group, int32_t table_offset, int32_t desc_size);
static void explore_blocks(struct ext2_group_desc * group_desc, int64_t blocks_per_group, char * block_bitmap);
static void explore_inode(int32_t local_inode_index, int32_t inode_table_block, int32_t inode_number);
static int blockIsInUse(char * block_bitmap, int32_t block_id);
static void claim(char * block_bitmap, int32_t block_id);

static int64_t getByteOffset(int32_t block_number);
static struct inode_node * mergeSort(struct inode_node * head, int32_t size);

int examineBlockArray(int * recover, char * block_bitmap, int32_t * block, int32_t num_elements); 
static void examineCandidates(char * block_bitmap);

struct inode_node {
  struct ext2_inode data;
  struct inode_node * next;
};

static struct ext2_super_block super_block;
static struct inode_node * RECOVERY_CANDIDATES = NULL;
static int32_t NUM_CANDIDATES = 0;
static int32_t fd;
static int32_t block_size;

int main (int argc, char* argv[]) {
  int32_t i, blocks_per_group, block_groups;
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
  
  // We'll read all of the block bitmaps into memory into this single array.
  char * block_bitmap = (char *) malloc((blocks_per_group * block_groups) / 8);
  int current_block_bitmap_offset = 0;

  // Flag for whether to take the s_first_ino into account.
  int first_block_flag = 1;

  // Next visit block group descriptor entry.
  for (i = 0; i < block_groups; i++) {

    // Seek to correct location of the group description. 
    int64_t cur_desc_offset = getGroupDescOffset(i, SUPERBLOCK_OFFSET + SUPERBLOCK_SIZE, sizeof(group_description));
    lseek(fd, cur_desc_offset, SEEK_SET);
   
    // Read the current group description.
    assert(read(fd, &group_description, sizeof(group_description)) == sizeof(group_description));

    // The first few inodes are not available for standard files, so we probably
    // shouldn't bother looking at them.
    int first_inode_index = 0;
    if (first_block_flag) {
      first_inode_index = super_block.s_first_ino;
      first_block_flag = 0;
    }

    // Explore all inodes in this block group.
    explore_inodes(&group_description, i, first_inode_index);
    
    // Fetch the block bitmap for this group. 
    explore_blocks(&group_description, blocks_per_group, &block_bitmap[current_block_bitmap_offset]);
    current_block_bitmap_offset += blocks_per_group / 8;
  }
  
  examineCandidates(block_bitmap);

  // Free the block bitmap.
  free(block_bitmap);

  assert(close(fd) == 0);
  return 0;
}

static int32_t * block_array;
static int32_t block_array_size;
static int32_t block_array_capacity;

/* Returns 1 if we should continue exmaining blocks in future indirect blocks. */
int examineBlockArray(int * recover, char * block_bitmap, int32_t * block, int32_t num_elements) {
  // Loop over each block pointed to by this inode. Look up the block number
  // in the block_bitmap, if in use we won't recover this file.
  // If a block is free, mark it as used and add to block_array.
  int32_t i;
  for (i = 0; i < num_elements; i++) {
    if (block[i] <= 0) {
     return 0;
    }
    if (blockIsInUse(block_bitmap, block[i])) {
      // This block has already been reclaimed.
      // We won't end up recovering this file.
      *recover = 0;
    } else {
      // Will add this block to our array.
      if (*recover) {
        // May need to resize block_array.
        if (block_array_size == block_array_capacity) {
          block_array_capacity = block_array_capacity * 2;
          block_array = (int32_t *) realloc(block_array, sizeof(int32_t) * block_array_capacity);
          assert(block_array != NULL);  // Not enough memory available to double
                                        // the size of the array.
                                        // That would be a pretty major issue
                                        // for this implementation, so let's
                                        // just fail.
        }
        block_array[block_array_size] = block[i];
        block_array_size++;
      }
      // Claim this block as 'used'
      claim(block_bitmap, block[i]);
    }
  }
  return 1;
}

int exploreSingleIndirectBlock(int32_t block_id_ptr, int * recover, char * block_bitmap) {
  lseek(fd, getByteOffset(block_id_ptr), SEEK_SET);
  int32_t * block = (int32_t *) malloc(block_size);
  assert(block != NULL);
  assert(read(fd, block, block_size) == block_size);
  int keepGoing = examineBlockArray(recover, block_bitmap, block, block_size / sizeof(int32_t));
  free(block);
  return keepGoing;
}

int exploreDoubleIndirectBlock(int32_t block_id_ptr, int * recover, char * block_bitmap) {
  int32_t i;
  lseek(fd, getByteOffset(block_id_ptr), SEEK_SET);
  int32_t * block = (int32_t *) malloc(block_size);
  assert(block != NULL);
  assert(read(fd, block, block_size) == block_size);
  int keepGoing = 1;
  for (i = 0; i < block_size / sizeof(int32_t); i++) {
    keepGoing = exploreSingleIndirectBlock(block[i], recover, block_bitmap);
    if (!keepGoing) {
      break;
    }
  }
  free(block);
  return keepGoing;
}

void examineCandidates(char * block_bitmap) {
  // Sort the candidate list with merge sort by comparing dtines (larger
  // dtime = earlier in the list).
  RECOVERY_CANDIDATES = mergeSort(RECOVERY_CANDIDATES, NUM_CANDIDATES);
  int32_t i;
  struct inode_node * current = RECOVERY_CANDIDATES;
  while (current != NULL) {
    block_array_size = 0;
    block_array_capacity = 12;
    block_array = (int32_t *) malloc(sizeof(int32_t) * block_array_capacity);
    assert(block_array != NULL);
    
    // Flag indicating whether to recover this file. Default to yes.
    int recover = 1;
     
    int keepGoing = examineBlockArray(&recover, block_bitmap, (int32_t *) current->data.i_block, 12); 
    
    if (keepGoing && current->data.i_block[12]) {
      // Explore the first indirect block.
      keepGoing = exploreSingleIndirectBlock(current->data.i_block[12], &recover, block_bitmap);
      if (keepGoing && current->data.i_block[13]) {
        // Explore the second indirect block.
        keepGoing = exploreDoubleIndirectBlock(current->data.i_block[13], &recover, block_bitmap);
      } if (keepGoing && current->data.i_block[14]) {
        // Explore the third indirect block.
        lseek(fd, getByteOffset(current->data.i_block[14]), SEEK_SET);
        int32_t * block = (int32_t *) malloc(block_size);
        assert(block != NULL);
        assert(read(fd, block, block_size) == block_size);
        for (i = 0; i < block_size / sizeof(int32_t); i++) {
          keepGoing = exploreDoubleIndirectBlock(block[i], &recover, block_bitmap);
          if (!keepGoing) {
            break;
          }
        }
        free(block);
      }
    } 
    
    // Caculate the expected number of data blocks.
    int64_t file_size = getFileSize(current->data.i_size, current->data.i_dir_acl);
    int64_t expected_blocks = file_size / block_size;
    if (file_size % block_size) {
      expected_blocks++;
    }
   
    // If the expected number of blocks doesn't match the recovered number of
    // blocks, something suspcious has happened so let's not recover this file.
    if (recover && expected_blocks == block_array_size) {
      // Recover this candidate.
      
      // Buffer to hold data blocks.
      char * the_block;

      // Open and create the file with proper name.
      char path[30];
      sprintf(path, "./recoveredFiles/file-%05d", current->data.i_ctime);
      int recoveredFile = open(path, O_RDWR | O_CREAT); 
      
      int64_t remaining_bytes = file_size;
      
      // Write all the data blocks of the file to the file.
      for (i = 0; i < block_array_size; i++) {
        int64_t size_to_write;
        if (block_size >= remaining_bytes) {
          size_to_write = remaining_bytes;
        } else {
          size_to_write = block_size;
        }
        lseek(fd, getByteOffset(block_array[i]), SEEK_SET);
        
        the_block = (char *) malloc(size_to_write);
        assert(the_block != NULL);
        read(fd, the_block, size_to_write);
        write(recoveredFile, the_block, size_to_write);
        remaining_bytes = remaining_bytes - size_to_write;
        free(the_block);
      } 
      
      // Set the modified and accessed times.
      struct timeval times[2];
      times[0].tv_sec = current->data.i_atime;
      times[1].tv_sec = current->data.i_mtime;
      futimes(recoveredFile, times);
 
      // Close the file.
      assert(close(recoveredFile) == 0);
    }
  
    // Free the block array.
    free(block_array);

    // Free this candidate and continue to the next.
    struct inode_node * temp = current;
    current = current->next;
    free(temp);
  }
}

/* Returns whether the given block_id is considered 'in use' according to the
 * given block bitmap. */
int blockIsInUse(char * block_bitmap, int32_t block_id) {
  int32_t index = block_id - super_block.s_first_data_block;
  char byte = block_bitmap[index / 8];
  int bit = (byte >> index % 8) & 1; 
  return bit;
} 

/* Mark the given block id as 'in use' in our in-memory block bitmap. */
void claim(char * block_bitmap, int32_t block_id){
  int32_t index = block_id - super_block.s_first_data_block;
  int16_t mask = 1 << (index % 8); 
  block_bitmap[index / 8] = block_bitmap[index / 8] | mask;
}

/* Returns the offset of the group description entry at the given index (group)
 * in the group description table. */
int64_t getGroupDescOffset(int32_t group, int32_t table_offset, int32_t desc_size) {
  return table_offset + (group * desc_size);  
}

/* Exploring a block bitmap entails reading the bitmap into the given block_bitmap
 * array at index 0. */
void explore_blocks(struct ext2_group_desc * group_desc, int64_t blocks_per_group, char * block_bitmap) {
  lseek(fd, getByteOffset(group_desc->bg_block_bitmap), SEEK_SET);
  assert(read(fd, block_bitmap, blocks_per_group / 8) == blocks_per_group / 8);
}

/* Loops over the inodes in the given group, exploring any that are 'free'. */
void explore_inodes(struct ext2_group_desc * group_desc, int32_t block_group, int32_t
  first_inode) {
  int32_t i;
  
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
      int32_t inode_number = 1 + (block_group * super_block.s_inodes_per_group) + i;
    if (bit == 0) {
      // This is a 'free' inode which may need to be undeleted.

      // Inodes start at inode 1. The inode number of this inode then must be 1
      // plus the number of inodes in previous blocks plus the current index in
      // this bitmap.
      explore_inode(i, group_desc->bg_inode_table, inode_number);  
   }
  }
  free(bitmap);
}

/* Explores the inode at the indicated index in the indicated table. If this
 * 'free' inode represents a file that could plausibly be recovered, we add it
 * to the RECOVERY_CANDIDATES linked list. */
void explore_inode(int32_t local_inode_index, int32_t inode_table_block, int32_t inode_number) {
  struct inode_node * inode; 

  // Need to grab the inode. First seek to the apporpriate byte offset.
  int64_t inode_byte_offset = super_block.s_inode_size * local_inode_index;
  int64_t byte = getByteOffset(inode_table_block) + inode_byte_offset;
  lseek(fd, byte, SEEK_SET);  

  // Read the inode.
  inode = (struct inode_node *) malloc(sizeof(struct inode_node));
  read(fd, inode, sizeof(struct ext2_inode));
   
  // We'll only deal with regular files with size greater than 0.
  uint16_t regular = (inode->data.i_mode >> 15) & 1;

  if (getFileSize(inode->data.i_size, inode->data.i_dir_acl) > 0 && regular) {
    // This is a candidate file which may have been deleted. 

    // First use the i_ctime field to store the indoe_number. We're not going to
    // write this to disk, but we want to keep the inode number associated with the
    // inode in case we end up recovering this file later.
    inode->data.i_ctime = inode_number;

    // Now we'll add this inode to the list of candidates.
    inode->next = RECOVERY_CANDIDATES; 
    RECOVERY_CANDIDATES = inode;
    NUM_CANDIDATES++;
  } else {
    // This is not a candidate for recovery.
    free(inode);
  }
}

/* Returns the size of a file given its lower 32 and upper 32 bits. Uses the
 * upper 32 bits depending on the revision number of the super block. */
int64_t getFileSize(int32_t lower_bits, int32_t upper_bits) {
  if (super_block.s_rev_level == 0) {
    return (int64_t) lower_bits;
  } else {
    int64_t upper = (int64_t) upper_bits;
    return (upper << 32) | lower_bits; 
  }
}

/* Returns the byte offset for the given block number. */
int64_t getByteOffset(int32_t block_number) {
  return (int64_t) block_size * block_number;
}

/* Performs merge sort on the given linked list of inode_nodes. */
struct inode_node * mergeSort(struct inode_node * head, int32_t size) {
  if (size <= 1) {
    return head;
  }

  // Split the list into halfs.
  int32_t i; 
  int32_t bound = size / 2;
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
