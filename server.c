#include <arpa/inet.h>
#include "common.c"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/md5.h>
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
    printf("Initiate a network file server\n");
    printf("  -l    number of entries in cache\n");
    printf("  -p    port on which to listen for connections\n");
}

/*
 * die() - print an error and exit the program
 */
void die(const char *msg1, char *msg2) {
    fprintf(stderr, "%s, %s\n", msg1, msg2);
    exit(0);
}

/*
 * open_server_socket() - Open a listening socket and return its file
 *                        descriptor, or terminate the program
 */
int open_server_socket(int port) {
    int                listenfd;    /* the server's listening file descriptor */
    struct sockaddr_in addrs;       /* describes which clients we'll accept */
    int                optval = 1;  /* for configuring the socket */

    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("Error creating socket: ", strerror(errno));

    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (const void *)&optval , sizeof(int)) < 0)
        die("Error configuring socket: ", strerror(errno));

    /* Listenfd will be an endpoint for all requests to the port from any IP
       address */
    bzero((char *) &addrs, sizeof(addrs));
    addrs.sin_family = AF_INET;
    addrs.sin_addr.s_addr = htonl(INADDR_ANY);
    addrs.sin_port = htons((unsigned short)port);
    if (bind(listenfd, (struct sockaddr *)&addrs, sizeof(addrs)) < 0)
        die("Error in bind(): ", strerror(errno));

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, 1024) < 0)  // backlog of 1024
        die("Error in listen(): ", strerror(errno));

    return listenfd;
}

/*
 * handle_requests() - given a listening file descriptor, continually wait
 *                     for a request to come in, and when it arrives, pass it
 *                     to service_function.  Note that this is not a
 *                     multi-threaded server.
 */
void handle_requests(int listenfd, void (*service_function)(int, int), int param) {
    while (1) {
        /* block until we get a connection */
        struct sockaddr_in clientaddr;
        int clientlen = sizeof(clientaddr);
        int connfd;
        if ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) < 0)
            die("Error in accept(): ", strerror(errno));

        /* print some info about the connection */
        struct hostent *hp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                           sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        if (hp == NULL) {
            fprintf(stderr, "DNS error in gethostbyaddr() %d\n", h_errno);
            exit(0);
        }
        char *haddrp = inet_ntoa(clientaddr.sin_addr);
        printf("server connected to %s (%s)\n", hp->h_name, haddrp);

        /* serve requests */
        service_function(connfd, param);

        /* clean up, await new connection */
        if (close(connfd) < 0)
            die("Error in close(): ", strerror(errno));
    }
}

/* sends the header of the get response to client */
void send_get_header(char* header, char * filename, int connfd){
  FILE * my_file;
  my_file=fopen(filename, "r");
  if(my_file==NULL){
    char * error = "ERROR (99): file not found";
    write(connfd,error,strlen(error)+1);
    return;
  }
  write(connfd, header, strlen(header));
  write(connfd, "\n", 1);
  write(connfd, filename, strlen(filename));
  write(connfd, "\n", 1);
  fseek(my_file, 0, SEEK_END);
  int size = ftell(my_file);
  fseek(my_file, 0, SEEK_SET);
  char filesize [1024];
  sprintf(filesize,"%d", size);
  write(connfd, filesize, strlen(filesize));
  write(connfd, "\n", 1);
  fclose(my_file);
}

// Sends back file specified by filename back to the client
void get_file(char * filename, int connfd){
  FILE * my_file;
  my_file=fopen(filename, "r");
  if(my_file==NULL){
    char * error = "ERROR (99): file not found";
    write(connfd,error,strlen(error)+1);
    return;
  }
  int file_no = fileno(my_file);

  void *buffer = malloc(TRANSFER_SIZE);
  int nread = read(file_no, buffer, TRANSFER_SIZE);
  void *ptr;
  while(nread != 0){
    int nsofar = 0;
    int nremain = nread;
    ptr = buffer;
    while (nremain > 0) {
      if ((nsofar = write(connfd, ptr, nremain)) <= 0) {
	if (errno != EINTR)
	  die("Write error: ", strerror(errno));
	fprintf(stderr, "Error with write\n");
      }
      nremain -= nsofar;
      ptr += nsofar;
    }
    nread = read(file_no, buffer, TRANSFER_SIZE);
  }
  fclose(my_file);
}

/*
 * file_server() - Read a request from a socket, satisfy the request, and
 *                 then close the connection.
 */
void file_server(int connfd, int lru_size) {
  /* TODO: set up a few static variables here to manage the LRU cache of
     files */
  
  // GET COMMAND FOR PUT OR GET
  char com_buf[MAX_LINE_SIZE];   // a place to store text from the client
  read_line(com_buf, MAX_LINE_SIZE, connfd);
  
  char filename_buf[MAX_LINE_SIZE]; //a place to store text from the client
  read_line(filename_buf, MAX_LINE_SIZE, connfd);

  if(strcmp(com_buf, "PUT") == 0){
    char bytesize_buf[MAX_LINE_SIZE];
    read_line(bytesize_buf, MAX_LINE_SIZE, connfd);
    //printf("%s\n", bytesize_buf);
    if(write_file(filename_buf, bytesize_buf, connfd, 0, NULL)){
      write(connfd, "OK\n", 3);
    }
  }else if(strcmp(com_buf, "GET") == 0){
    send_get_header("OK",filename_buf, connfd);
    get_file(filename_buf, connfd);
  }
  else if(strcmp(com_buf, "PUTC") == 0){
    char bytesize_buf[MAX_LINE_SIZE];
    read_line(bytesize_buf, MAX_LINE_SIZE, connfd);
    //printf("%s\n", bytesize_buf);
    unsigned char md5_incoming[MD5_HASH_SIZE];
    read_line(md5_incoming, MD5_HASH_SIZE, connfd);
    //write(STDERR_FILENO, md5_incoming, MD5_HASH_SIZE);
    unsigned char * md5_buffer = malloc(MD5_HASH_SIZE);
    if(write_file(filename_buf, bytesize_buf, connfd, 1, md5_buffer)){
      fprintf(stderr, "%s\n%s\n", md5_incoming, md5_buffer);
      if(memcmp(md5_buffer, md5_incoming, MD5_HASH_SIZE) == 0){
	write(connfd, "OKC\n", 4);
      }else{
	write(connfd, "ERROR (102): Checksum does not match\n", 37);
      }
    }
    free(md5_buffer);
  }
  else if(strcmp(com_buf, "GETC") == 0){
    unsigned char *md5_buf = malloc(MD5_HASH_SIZE);
    compute_md5(filename_buf, md5_buf); 
    fprintf(stderr, "%s\n", md5_buf);
    send_get_header("OKC",filename_buf, connfd);
    write(connfd, md5_buf, MD5_HASH_SIZE);
    write(connfd, "\n", 1);
    get_file(filename_buf, connfd);
    free(md5_buf);
  }
  else{
    fprintf(stderr, "Command not recognized");
  }
}

/*
 * main() - parse command line, create a socket, handle requests
 */
int main(int argc, char **argv) {
    /* for getopt */
    long opt;
    /* NB: the "part 3" behavior only should happen when lru_size > 0 */
    int  lru_size = 0;
    int  port     = 9000;

    check_team(argv[0]);

    /* parse the command-line options.  They are 'p' for port number,  */
    /* and 'l' for lru cache size.  'h' is also supported. */
    while ((opt = getopt(argc, argv, "hl:p:")) != -1) {
        switch(opt) {
          case 'h': help(argv[0]); break;
          case 'l': lru_size = atoi(argv[0]); break;
          case 'p': port = atoi(optarg); break;
        }
    }

    /* open a socket, and start handling requests */
    int fd = open_server_socket(port);
    handle_requests(fd, file_server, lru_size);

    exit(0);
}
