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

int main (int argc, char* argv[]) {
  int fd, ret;
  ext2_super_block super_block;

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
  ret = lseek(fd, SUPERBLOCK_OFFSET, SEEK_SET);
  assert(ret == SUPERBLOCK_OFFSET);
  
  assert(read(fd, &super_block, sizeof(super_block)) == sizeof(super_block));
  printf("%d", super_block.s_inodes_count);

  // Visit each 'free' inode and print:
  // * inode number
  // * delete time
  // * file size
  // * each block it points to
  // * time last modified
  // * time last accessed
  assert(close(fd) == 0);
  return 0;
}
