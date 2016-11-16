
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "support.h"
#include <openssl/md5.h>
/*
 * help() - Print a help message
 */
void help(char *progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Perform a PUT or a GET from a network file server\n");
    printf("  -P    PUT file indicated by parameter\n");
    printf("  -G    GET file indicated by parameter\n");
    printf("  -C    use checksums for PUT and GET\n");
    printf("  -e    use encryption, with public.pem and private.pem\n");
    printf("  -s    server info (IP or hostname)\n");
    printf("  -p    port on which to contact server\n");
    printf("  -S    for GETs, name to use when saving file locally\n");
}

/*
 * die() - print an error and exit the program
 */
void die(const char *msg1, char *msg2) {
    fprintf(stderr, "%s, %s\n", msg1, msg2);
    exit(0);
}

/*
 * connect_to_server() - open a connection to the server specified by the
 *                       parameters
 */
int connect_to_server(char *server, int port) {
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;
    char errbuf[256];                                   /* for errors */

    /* create a socket */
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("Error creating socket: ", strerror(errno));

    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(server)) == NULL) {
        sprintf(errbuf, "%d", h_errno);
        die("DNS error: DNS error ", errbuf);
    }
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0],
          (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    /* connect */
    if (connect(clientfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        die("Error connecting: ", strerror(errno));
    return clientfd;
}

/* computes the MD5 of a file */
void compute_md5(char * filename, unsigned char * md5_buffer){
  FILE * file;
  file = fopen(filename, "r");
  if(file == NULL){
    die("Could not open file", strerror(errno));
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
	die("read error: ", strerror(errno));
      }
      continue;
    }
    if(nread == 0){
      MD5_Final(md5_buffer, &c);
      fclose(file);
      free(buf);
      return;
    }
    MD5_Update(&c, buf, strlen(buf));
  }
}

/*
 * put_file() - send a file to the server accessible via the given socket fd
 */
void put_header(int fd, char* put_name,int check_sum_flag){

  if(check_sum_flag==1){
     write(fd, "PUTC\n", 5);
  }
  else {
     write(fd, "PUT\n", 4);
   }
  write(fd, put_name, strlen(put_name));
  write(fd, "\n", 1);
  FILE * my_file;
  my_file=fopen(put_name, "r");
  if(my_file==NULL){
    die("issue with fopen",put_name);
  }
  fseek(my_file, 0, SEEK_END);
  int size = ftell(my_file);
  fseek(my_file, 0, SEEK_SET);
  fclose(my_file);
  char filesize [1024];
  sprintf(filesize,"%d", size);
  write(fd, filesize, strlen(filesize));
  write(fd, "\n", 1);
  if(check_sum_flag==1){
    unsigned char * md5 = malloc(16);
    compute_md5(put_name, md5);
    write(fd, md5, 16);
    write(fd, "\n", 1);
    fprintf(stderr, "%s\n", md5);
    free(md5);
  }
}
  
void put_file(int fd, char *put_name) {
  FILE * my_file;
  my_file=fopen(put_name, "r");
  if(my_file==NULL){
    die("issue with fopen",put_name);
  }
  int file_no = fileno(my_file);

  int MAXSIZE = 256;
  void *buffer = malloc(MAXSIZE);
  int nread = read(file_no, buffer, MAXSIZE);
  void * ptr;
  while(nread != 0){
    int nsofar = 0;
    int nremain = nread;
    ptr = buffer;
    while (nremain > 0) {     
      if ((nsofar = write(fd, ptr, nremain)) <= 0) {
	if (errno != EINTR)
	  die("Write error: ", strerror(errno));
	fprintf(stderr, "Error with write\n");
      }
      nremain -= nsofar;
      ptr += nsofar;
    }
    nread = read(file_no, buffer, MAXSIZE);//see above
  }
  fclose(my_file);

  char read_buffer[1024];
  read(fd, read_buffer, 1023);
  printf("RECIEVED: %s\n", read_buffer);
}

/* Reads single line of input from socket, leaving rest in the socket */
void read_line(char * buffer, int bufsize, int connfd){
  bzero(buffer, bufsize);

  /* read from socket, recognizing that we may get short counts */
  char *bufp = buffer;          /* current pointer into buffer */
  ssize_t nremain = bufsize; /* max characters we can still read*/
  size_t nsofar;                 /* characters read so far */
  while (1) {
    /* read some data; swallow EINTRs */
    if ((nsofar = read(connfd, bufp, 1)) < 0) {
      if (errno != EINTR)
	die("read error: ", strerror(errno));
      continue;
    }
    /* end service to this client on EOF */
    if (nsofar == 0) {
      fprintf(stderr, "received EOF\n");
      return;
    }
    /* update pointer for next bit of reading */
    bufp += nsofar;
    nremain -= nsofar;
    if (*(bufp-1) == '\n') {
      *(bufp-1) = 0;
      break;
    }
  }
}

/*
 * get_file() - get a file from the server accessible via the given socket
 *              fd, and save it according to the save_name
 */
void get_header(int fd, char * get_name, int check_sum_flag){
  if(check_sum_flag==1){
    write(fd,"GETC\n",5);
  }
  else {
    write(fd,"GET\n",4);   
  }
  write(fd, get_name, strlen(get_name));
  write(fd, "\n", 1);
}


void get_file(int fd, char *get_name, char *save_name, int checksum_flag) {  
  char response_buf [1024];
  read_line(response_buf, 1024, fd);
  char filename_buf [1024];
  read_line(filename_buf, 1024, fd);
  if(strcmp(filename_buf, get_name) != 0){
    die("read error:", "file returned is not correct");
  }
  char bytesize_buf[1024];
  read_line(bytesize_buf, 1024, fd);
  
  int num_expected_bytes = atoi(bytesize_buf);
  int num_actual_bytes = 0;

  unsigned char md5_incoming[16];
  unsigned char md5_computed[16];
  if(checksum_flag){
    read_line(md5_incoming, 16, fd);
  }
  MD5_CTX c;
  if(checksum_flag){
    MD5_Init(&c);
  }
  
  FILE * write_file;
  write_file = fopen(save_name, "w");
  if(write_file == NULL){
    // die("write error: ", strerror(errno));
    write_file=stdout;
  }
  int file_no=fileno(write_file);
  int MAXSIZE = 256;
  void *buf = malloc(MAXSIZE);
  
  // read from socket, recognizing that we may get short counts
  size_t nread; //number of bytes read
  while (1) {
     bzero(buf, MAXSIZE);
    // read some data; swallow EINTRs
    if ((nread = read(fd, buf, MAXSIZE)) < 0) {
      if (errno != EINTR){
	fclose(write_file);
	free(buf);
	die("read error: ", strerror(errno));
      }
      continue;
    }
    // fprintf(write_file, "%s", buf);
    write(file_no,buf,nread);
    if(checksum_flag && nread != 0){
      MD5_Update(&c, buf, nread);
    }
    num_actual_bytes += nread;
      // end service to this client on EOF
    if(num_actual_bytes == num_expected_bytes){
      fclose(write_file);
      free(buf);
      if(checksum_flag){
	MD5_Final(md5_computed, &c);
	fprintf(stderr, "%s\n%s\n", md5_incoming, md5_computed);
	if(strcmp(md5_incoming, md5_computed) != 0){
	  die("ERROR (101):", "Checksums do not match\n");
	}
      }
      return;
    }
    if(nread == 0){
      die("ERROR (99):", "Number of bytes read not what expected\n");
      fclose(write_file);
      free(buf);
      return;
    }
  }
  free(buf);
}

/*
 * main() - parse command line, open a socket, transfer a file
 */
int main(int argc, char **argv) {
    /* for getopt */
    long  opt;
    char *server = NULL;
    char *put_name = NULL;
    char *get_name = NULL;
    int   port;
    char *save_name = NULL;
    int check_sum_flag=0;
    check_team(argv[0]);

    /* parse the command-line options. */
    /* TODO: add additional opt flags */
    while ((opt = getopt(argc, argv, "hCs:P:G:S:p:")) != -1) {
        switch(opt) {
          case 'h': help(argv[0]); break;
          case 's': server = optarg; break;
          case 'P': put_name = optarg; break;
          case 'G': get_name = optarg; break;
          case 'S': save_name = optarg; break;
	  case 'C': check_sum_flag=1;break;
	  case 'p': port = atoi(optarg); break;
        }
    }

    /* open a connection to the server */
    int fd = connect_to_server(server, port);
    if(check_sum_flag==1){

      //      printf("c arg worked");


      
    }
    /* put or get, as appropriate */
    if (put_name){
      put_header(fd,put_name,check_sum_flag);
      put_file(fd, put_name);
    }
    else{
      get_header(fd,get_name, check_sum_flag);
      get_file(fd, get_name, save_name, check_sum_flag);
    }
    /* close the socket */
    int rc;
    if ((rc = close(fd)) < 0)
        die("Close error: ", strerror(errno));
    exit(0);
}
