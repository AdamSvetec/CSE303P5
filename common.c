#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <openssl/md5.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>

#define TRANSFER_SIZE 256
#define MAX_LINE_SIZE 1024
#define MD5_HASH_SIZE 16

/* Reads single line of input from socket, leaving rest in the socket */
int read_line(char * buffer, int bufsize, int connfd){

  //TODO: need to add check so there are no overflows

  bzero(buffer, bufsize);
  /* read from socket, recognizing that we may get short counts */
  char *bufp = buffer;          /* current pointer into buffer */
  ssize_t nremain = bufsize; /* max characters we can still read*/
  size_t nread;                 /* characters read so far */
  while (1) {
    /* read some data; swallow EINTRs */
    if ((nread = read(connfd, bufp, 1)) < 0) {
      if (errno != EINTR){
	fprintf(stderr, "read error: %s", strerror(errno));
	return 0;
      }
      continue;
    }
    /* end service to this client on EOF */
    if (nread == 0) {
      fprintf(stderr, "received EOF\n");
      return 0;
    }
    /* update pointer for next bit of reading */
    bufp += nread;
    if (*(bufp-1) == '\n') {
      *(bufp-1) = 0;
      break;
    }
  }
  return 1;
}

/* computes the MD5 of a file */
int compute_md5(char * filename, unsigned char * md5_buffer){
  if(md5_buffer != NULL){
    free(md5_buffer);
  }
  md5_buffer = malloc(MD5_HASH_SIZE);
  
  FILE * file;
  file = fopen(filename, "r");
  if(file == NULL){
    fprintf(stderr,"Could not open file: %s", strerror(errno));
    return 0;
  }
  int file_no = fileno(file);

  MD5_CTX c;
  MD5_Init(&c);

  void *buf = malloc(TRANSFER_SIZE);
  size_t nread;
  while (1) {
    bzero(buf, TRANSFER_SIZE);
    // read some data; swallow EINTRs
    if ((nread = read(file_no, buf, TRANSFER_SIZE)) < 0) {
      if (errno != EINTR){
	fclose(file);
	free(buf);
	fprintf(stderr,"read error: %s", strerror(errno));
	return 1;
      }
      continue;
    }
    if(nread == 0){
      MD5_Final(md5_buffer, &c);
      fclose(file);
      free(buf);
      return 1;
    }
    MD5_Update(&c, buf, nread);
  }
}
