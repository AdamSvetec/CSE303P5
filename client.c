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

/*
 * put_file() - send a file to the server accessible via the given socket fd
 */
void put_file(int fd, char *put_name) {
  FILE * my_file;
  my_file=fopen(put_name, "r");
  if(my_file==NULL){
    die("issue with fopen",put_name);
  }
  //Write PUT
  write(fd, "PUT\n", 4);
  //Write filename
  write(fd, put_name, strlen(put_name));
  write(fd, "\n", 1);
  //Write file size
  fseek(my_file, 0, SEEK_END);
  int size = ftell(my_file);
  fseek(my_file, 0, SEEK_SET);
  char filesize [1024];
  sprintf(filesize,"%d", size);
  write(fd, filesize, strlen(filesize));
  write(fd, "\n", 1);

  char* c;
  char buffer[256];
  c=fgets(buffer,255,my_file);//255 makes sure there will always be
			      //null character at end of buffer
  while(c != NULL){
    int nsofar = 0;
    int nremain = strlen(buffer);
    while (nremain > 0) {     
      if ((nsofar = write(fd, c, nremain)) <= 0) {
	if (errno != EINTR)
	  die("Write error: ", strerror(errno));
	nsofar = 0;
      }
      nremain -= nsofar;
      c += nsofar;
    }
    c=fgets(buffer,255,my_file);//see above
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
void get_file(int fd, char *get_name, char *save_name) {
  //Write PUT
  write(fd, "GET\n", 4);
  //Write filename
  write(fd, get_name, strlen(get_name));
  write(fd, "\n", 1);

  FILE * write_file;
  write_file = fopen(save_name, "w");
  if(write_file == NULL){
    die("write error: ", strerror(errno));
  }

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
  while (1) {
    const int MAXLINE = 8192;
    char buf[MAXLINE]; // a place to store text from the client
    bzero(buf, MAXLINE);

    // read from socket, recognizing that we may get short counts
    size_t nread; //number of bytes read
    while (1) {
      // read some data; swallow EINTRs
      if ((nread = read(fd, buf, MAXLINE)) < 0) {
	if (errno != EINTR)
	  fclose(write_file);
	die("read error: ", strerror(errno));
	continue;
      }
      fprintf(write_file, "%s", buf);
      num_actual_bytes += nread;
      // end service to this client on EOF
      if(num_actual_bytes == num_expected_bytes){
	fclose(write_file);
	return;
      }
      if(nread == 0){
	die("ERROR (99):", "Number of bytes read not what expected\n");
	fclose(write_file);
	return;
      }
    }
  }
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

    check_team(argv[0]);

    /* parse the command-line options. */
    /* TODO: add additional opt flags */
    while ((opt = getopt(argc, argv, "hs:P:G:S:p:")) != -1) {
        switch(opt) {
          case 'h': help(argv[0]); break;
          case 's': server = optarg; break;
          case 'P': put_name = optarg; break;
          case 'G': get_name = optarg; break;
          case 'S': save_name = optarg; break;
          case 'p': port = atoi(optarg); break;
        }
    }

    /* open a connection to the server */
    int fd = connect_to_server(server, port);

    /* put or get, as appropriate */
    if (put_name)
        put_file(fd, put_name);
    else
        get_file(fd, get_name, save_name);

    /* close the socket */
    int rc;
    if ((rc = close(fd)) < 0)
        die("Close error: ", strerror(errno));
    exit(0);
}
