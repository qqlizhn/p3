#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <error.h>

//-----------------------------------------------------
// Sample program doing raw file IO.
// Writes an array of N elements to a file.
// Reads them back, in reverse order and prints them.
//-----------------------------------------------------

#define N 10
#define FILENAME ".weeville"
// Note that the filename is a "hidden file" (the filename starts with a period)

// this establishes the type name 'struct weeville'
struct weeville {
  int  intPart;
  char charPart;
};

int main(int argc, char* argv[]) {
  int w;
  int fd;
  int rcode;

  // open the file
  fd = open( FILENAME, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
  if ( fd < 0 ) error(-1, errno, "write open");

  // initialize and write N struct weeville's to the file
  struct weeville source[N];
  for ( w=0; w<N; w++ ) {
    source[w].intPart = w;
    source[w].charPart = 'A' + (w % 26);
  }
  rcode = write( fd, &source, sizeof(source) );
  if ( rcode < 0 ) error(-1, errno, "write");

  if ( close(fd) ) error(-1, errno, "write close");

  // now read them back, one at a time, in reverse order

  fd = open( FILENAME, O_RDONLY );
  if ( fd < 0 ) error(-1, errno, "read open");

  struct weeville dest;
  for ( w=N-1; w>=0; w-- ) {
    rcode = lseek( fd, w*sizeof(struct weeville), SEEK_SET );
    if ( rcode < 0 ) error( -1, errno, "lseek");
    rcode = read( fd, &dest, sizeof(struct weeville) );
    if ( rcode < 0 ) error(-1, errno, "read");
    printf( "%05d-%c\n", dest.intPart, dest.charPart );
  }

  if ( close(fd) ) error(-1, errno, "read close");

  return 0;
}
