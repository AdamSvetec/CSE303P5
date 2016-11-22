#include <arpa/inet.h>
#include "common.c"
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

int encrypted_file_size(char * filename){
  FILE * file = fopen(filename, "r");
  if(file == NULL){
    die("Could not open file: ", strerror(errno));
  }
  int file_no = fileno(file);
  
  RSA * rsa = get_pub_rsa();

  int RSA_SIZE = RSA_size(rsa);
  void * buffer = malloc(RSA_SIZE/8);
  void * encrypted = malloc(RSA_SIZE);
  int total_size = 0;
  int nread = read(file_no, buffer, RSA_SIZE/8);
  while(nread > 0){
    int size = RSA_public_encrypt(nread,buffer,encrypted,rsa,PADDING);
    //fprintf(stderr, "Read size: %d\n", size);
    total_size+= size;
    nread = read(file_no, buffer, RSA_SIZE/8);
  }
  fclose(file);
  return total_size;
}

/*
 * put_file() - send a file to the server accessible via the given socket fd
 */
void put_header(int fd, char* put_name,int check_sum_flag, int encrypt_flag){
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
  fclose(my_file);

  if(encrypt_flag){
    size = encrypted_file_size(put_name);
  }
  
  char filesize [1024];
  sprintf(filesize,"%d", size);
  write(fd, filesize, strlen(filesize));
  write(fd, "\n", 1);
  if(check_sum_flag==1){
    unsigned char * md5 = malloc(MD5_HASH_SIZE);
    compute_md5(put_name, md5);
    write(fd, md5, MD5_HASH_SIZE);
    write(fd, "\n", 1);
    //write(STDERR_FILENO, md5, MD5_HASH_SIZE);
    free(md5);
  }
}
  
void put_file(int fd, char *put_name, int encrypt_flag) {
  FILE * my_file;
  my_file=fopen(put_name, "r");
  if(my_file==NULL){
    die("issue with fopen",put_name);
  }
  int file_no = fileno(my_file);

  int READ_SIZE = TRANSFER_SIZE;
  RSA * rsa;
  if(encrypt_flag){
    rsa = get_pub_rsa();
    READ_SIZE = RSA_size(rsa)/8;
  }
  
  void *buffer = malloc(READ_SIZE);
  void *copy_buffer = malloc(READ_SIZE*8);
  int nread = read(file_no, buffer, READ_SIZE);
  void * ptr;
  while(nread > 0){
    int nsofar = 0;
    if(encrypt_flag){
      nread = RSA_public_encrypt(nread,buffer,copy_buffer,rsa,PADDING);
    }else{
      memcpy(copy_buffer, buffer, nread);
    }
    int nremain = nread;
    ptr = copy_buffer;
    while (nremain > 0) {     
      if ((nsofar = write(fd, ptr, nremain)) <= 0) {
	if (errno != EINTR)
	  die("Write error: ", strerror(errno));
	fprintf(stderr, "Error with write\n");
      }
      nremain -= nsofar;
      ptr += nsofar;
    }
    nread = read(file_no, buffer, READ_SIZE);//see above
  }
  fclose(my_file);
  free(buffer);

  char read_buffer[1024];
  read(fd, read_buffer, 1023);
  printf("RECEIVED: %s\n", read_buffer);
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
    int encrypt_flag=0;
    check_team(argv[0]);

    /* parse the command-line options. */
    /* TODO: add additional opt flags */
    while ((opt = getopt(argc, argv, "heCs:P:G:S:p:")) != -1) {
        switch(opt) {
	case 'h': help(argv[0]); exit(1);break;
	case 's': server = optarg; break;
	case 'P': put_name = optarg; break;
	case 'G': get_name = optarg; break;
	case 'S': save_name = optarg; break;
	case 'C': check_sum_flag=1;break;
	case 'p': port = atoi(optarg); break;
	case 'e': encrypt_flag = 1; break;
	}
    }

    /* open a connection to the server */
    int fd = connect_to_server(server, port);

    if(put_name){
      put_header(fd,put_name,check_sum_flag, encrypt_flag);
      put_file(fd, put_name, encrypt_flag);
    }
    else{
      get_header(fd,get_name, check_sum_flag);
      char response_buf [MAX_LINE_SIZE];
      read_line(response_buf, MAX_LINE_SIZE, fd);
      if(strcmp(response_buf, "OKC") != 0 && strcmp(response_buf, "OK") != 0){
	die("Get error: ",response_buf);
      }
      char filename_buf [MAX_LINE_SIZE];
      read_line(filename_buf, MAX_LINE_SIZE, fd);
      if(strcmp(filename_buf, get_name) != 0){
	die("read error:", "file returned is not correct");
      }
      char bytesize_buf[MAX_LINE_SIZE];
      read_line(bytesize_buf, MAX_LINE_SIZE, fd);
      unsigned char md5_incoming[MD5_HASH_SIZE];
      if(check_sum_flag){
	read_line(md5_incoming, MD5_HASH_SIZE, fd);
      }
      unsigned char md5_cs[MD5_HASH_SIZE];      
      if(write_file(save_name, bytesize_buf, fd, check_sum_flag, md5_cs, encrypt_flag)){
	if(check_sum_flag){
	  if(memcmp(md5_incoming, md5_cs, MD5_HASH_SIZE) != 0){
	    die("ERROR (103): ", "Checksums do not match");
	  }
	}
	printf("Received File Successfully\n");
      }else{
	die("ERROR: ", "error writing file");
      }
    }
    /* close the socket */
    int rc;
    if ((rc = close(fd)) < 0)
        die("Close error: ", strerror(errno));
    exit(0);
}
