#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <openssl/md5.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <openssl/pem.h>

#define PUB_KEY_FILE "public.pem"
#define PRIV_KEY_FILE "private.pem"
#define PADDING RSA_PKCS1_PADDING

#define TRANSFER_SIZE 256
#define MAX_LINE_SIZE 1024
#define MD5_HASH_SIZE 16



RSA * get_pub_rsa(){
  FILE * key = fopen(PUB_KEY_FILE, "r");
  if(key == NULL){
    fprintf(stderr, "Could not open public key file\n");
    return NULL;
  }
  RSA * rsa = RSA_new();
  rsa = PEM_read_RSA_PUBKEY(key, &rsa, NULL, NULL);
  if(rsa == NULL){
    fprintf(stderr, "RSA key is null\n");
  }
  return rsa;
}

RSA * get_priv_rsa(){
  FILE * key = fopen(PRIV_KEY_FILE, "r");
  if(key == NULL){
    fprintf(stderr, "Could not open public key file\n");
    return NULL;
  }
  RSA * rsa = RSA_new();
  rsa = PEM_read_RSAPrivateKey(key, &rsa, NULL, NULL);
  if(rsa == NULL){
    fprintf(stderr, "RSA key is null\n");
  }
  return rsa;
}



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
int compute_md5(char * filename, unsigned char * md5_buffer, int encrypt_flag){
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

  RSA * rsa;
  int READ_SIZE=TRANSFER_SIZE;
  if(encrypt_flag){
    rsa=get_pub_rsa();
    READ_SIZE=RSA_size(rsa)/8;
  }
  void *buf = malloc(READ_SIZE);
  void * encrypted_buf=malloc(8*READ_SIZE);
  
  size_t nread;
  while (1) {
    bzero(buf, READ_SIZE);
    // read some data; swallow EINTRs
    if ((nread = read(file_no, buf,READ_SIZE)) < 0) {
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
    if(encrypt_flag){
      nread=RSA_public_encrypt(nread,buf,encrypted_buf,rsa,PADDING);
      MD5_Update(&c,encrypted_buf,nread);
    }
    else{
      MD5_Update(&c, buf, nread);
    }
  }
}

//Writes all bits recieved to new or overwritten file at filename
int write_file(char* filename, char* numbytes, int connfd, int md5_flag, unsigned char* md5_cs, int decrypt_flag){
  FILE * write_file;
  write_file = fopen(filename, "w");
  if(write_file == NULL){
    fprintf(stderr,"Write Error: %s", strerror(errno));
    return 0;
  }
  int file_no = fileno(write_file);

  int num_expected_bytes = atoi(numbytes);
  int num_actual_bytes = 0;

  MD5_CTX c;
  if(md5_flag){
    MD5_Init(&c);
  }

  int READ_SIZE = TRANSFER_SIZE;
  RSA * rsa;
  if(decrypt_flag){
    rsa = get_priv_rsa();
    READ_SIZE = RSA_size(rsa);
  }
  void *buf = malloc(READ_SIZE);   // a place to store text from
  void * copy_buf = malloc(READ_SIZE);
  
  size_t nread; //number of bytes read
  while (1) {
    bzero(buf, READ_SIZE);
    if ((nread = read(connfd, buf, READ_SIZE)) < 0) {
      if (errno != EINTR){
	fclose(write_file);
	free(buf);
	fprintf(stderr, "read error: %s", strerror(errno));
	return 0;
      }
      continue;
    }
    int nwrite = nread;
    if(decrypt_flag){
      nwrite = RSA_private_decrypt(nread,buf,copy_buf,rsa,PADDING);
    }else{
      memcpy(copy_buf, buf, nread);
    }
    write(file_no, copy_buf, nwrite);
    //write(STDERR_FILENO, copy_buf, nwrite);
    if(md5_flag && nread > 0){
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
  return 0;
}
