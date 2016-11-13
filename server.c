#include <arpa/inet.h>
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

//Writes all bits recieved to new or overwritten file at filename
void put_file(char * filename, char * numbytes, int connfd){
  FILE * write_file;
  //Actual Implementation: write_file = fopen(filename, "w");
  write_file = fopen("testingwrite.txt", "w"); //Just for testing purposes
  if(write_file == NULL){
    die("write error: ", strerror(errno));
  }
  int num_expected_bytes = atoi(numbytes);
  int num_actual_bytes = 0;
  while (1) {
    const int MAXLINE = 8192;
    char buf[MAXLINE];   // a place to store text from the client
    bzero(buf, MAXLINE);

    // read from socket, recognizing that we may get short counts
    size_t nread; //number of bytes read
    while (1) {
      // read some data; swallow EINTRs
      if ((nread = read(connfd, buf, MAXLINE)) < 0) {
	if (errno != EINTR)
	  fclose(write_file);
	  die("read error: ", strerror(errno));
	continue;
      }
      fprintf(write_file, "%s", buf);
      num_actual_bytes += nread; 
      // end service to this client on EOF
      if(num_actual_bytes == num_expected_bytes){
	write(connfd, "OK\n", 4);
	fclose(write_file);
	return;
      }
      if(nread == 0){
	char * error = "ERROR (99): Number of bytes read not what expected\n";
	write(connfd, error, strlen(error)+1);
	fclose(write_file);
	return;
      }
    }
  }
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
  //Write PUT
  write(connfd, "OK\n", 3);
  //Write filename
  write(connfd, filename, strlen(filename));
  write(connfd, "\n", 1);
  //Write file size
  fseek(my_file, 0, SEEK_END);//finds the end of the file
  int size = ftell(my_file);//tells you where on the file you are part
			    //of size
  fseek(my_file, 0, SEEK_SET);
  char filesize [1024];
  sprintf(filesize,"%d", size);
  write(connfd, filesize, strlen(filesize));
  write(connfd, "\n", 1);

  char* c;
  char buffer[256];
  c=fgets(buffer,255,my_file);
  while(c != NULL){
    int nsofar = 0;
    int nremain = strlen(buffer);
    while (nremain > 0) {
      if ((nsofar = write(connfd, c, nremain)) <= 0) {
	if (errno != EINTR)
	  die("Write error: ", strerror(errno));
	nsofar = 0;
      }
      nremain -= nsofar;
      c += nsofar;
    }
    c=fgets(buffer,255,my_file);
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
  const int COM_MAXLINE = 1024;
  char com_buf[COM_MAXLINE];   // a place to store text from the client
  read_line(com_buf, COM_MAXLINE, connfd);
  
  char filename_buf[COM_MAXLINE]; //a place to store text from the client
  read_line(filename_buf, COM_MAXLINE, connfd);
  //above code fills first buffer with PUT or GET,
  //Then it fills the second with the filename
  if(strcmp(com_buf, "PUT") == 0){
    char bytesize_buf[COM_MAXLINE];
    read_line(bytesize_buf, COM_MAXLINE, connfd);
    //printf("%s\n", bytesize_buf);
    put_file(filename_buf, bytesize_buf, connfd);
  }else if(strcmp(com_buf, "GET") == 0){
    get_file(filename_buf, connfd);
  }
  else if(strcmp(com_buf, "PUTC") == 0){
    //TODO add checksum functionality
    char bytesize_buf[COM_MAXLINE];
    read_line(bytesize_buf, COM_MAXLINE, connfd);
    //printf("%s\n", bytesize_buf);
    put_file(filename_buf, bytesize_buf, connfd);
 
  }
  else if(strcmp(com_buf, "GETC") == 0){
    get_file(filename_buf, connfd);
    //TODO add checksum Functionality
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
