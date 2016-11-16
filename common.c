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
    nremain -= nread;
    if (*(bufp-1) == '\n') {
      *(bufp-1) = 0;
      break;
    }
    if(nremain == 0){
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
  md5_buffer = malloc(16);
  
  FILE * file;
  file = fopen(filename, "r");
  if(file == NULL){
    fprintf(stderr,"Could not open file: %s", strerror(errno));
    return 0;
  }
  int file_no = fileno(file);

  MD5_CTX c;
  MD5_Init(&c);

  int MAXSIZE = 256;
  void *buf = malloc(MAXSIZE);
  size_t nread;
  while (1) {
    bzero(buf, MAXSIZE);
    // read some data; swallow EINTRs
    if ((nread = read(file_no, buf, MAXSIZE)) < 0) {
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
    MD5_Update(&c, buf, strlen(buf));
  }
}


//Writes all bits recieved to new or overwritten file at filename
int write_file(char* filename, char* numbytes, int connfd, int md5_flag, unsigned char* md5_cs){
  FILE * write_file;
  write_file = fopen(filename, "w");
  if(write_file == NULL){
    fprintf(stderr,"write error: %s", strerror(errno));
    return 0;
  }
  int file_no = fileno(write_file);

  int num_expected_bytes = atoi(numbytes);
  int num_actual_bytes = 0;

  MD5_CTX c;
  if(md5_flag){
    MD5_Init(&c);
  }

  void *buf = malloc(TRANSFER_SIZE);   // a place to store text from
  size_t nread; //number of bytes read
  
  while (1) {
    bzero(buf, TRANSFER_SIZE);
    if ((nread = read(connfd, buf, TRANSFER_SIZE)) < 0) {
      if (errno != EINTR){
	fclose(write_file);
	free(buf);
	fprintf(stderr, "read error: %s", strerror(errno));
	return 0;
      }
      continue;
    }
    write(file_no, buf, nread);
    //fprintf(stderr, "%s", buf);
    if(md5_flag && nread != 0){ //TODO: check this conditional
      MD5_Update(&c, buf, nread);
    }
    num_actual_bytes += nread;
    if(num_actual_bytes == num_expected_bytes){
      free(buf);
      fclose(write_file);
      if(md5_flag){
	MD5_Final(md5_cs, &c);
      }
      return 1;
    }
    if(nread == 0){
      char * error = "ERROR (99): Number of bytes read not what expected\n";
      write(connfd, error, strlen(error)+1);
      free(buf);
      fclose(write_file);
      return 0;
    }
  }
}
