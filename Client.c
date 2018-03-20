#include "dtp.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <arpa/inet.h>

#include <sys/stat.h>

/* Returns size of file. */
size_t get_filesize(const char* filename) {
  struct stat file_stats;
  stat(filename, &file_stats);
  return file_stats.st_size;
}

int main (int argc, char *argv[]) {
  if( argc != 4 ) {
    fprintf(stderr, "Usage: %s <server_ip> <server_port> <filename>\n", argv[0]);
    return 1;
  }

  const char* server_ip = argv[1];
  port_t server_port = atoi(argv[2]);
  int stat;
  dtp_client client;
  stat = init_dtp_client(&client, server_ip, server_port);
  if( stat < 0 ) {
    perror("Init client :");
    return 1;
  }

  stat = dtp_connect(&client);
  if( stat < 0 ) {
    perror("Connect :");
    return 1;
  }

  printf("Client connected to server from port %u\n",
	 ntohs(client.self.sin_port));
  printf("(%d) :: %s:%u\n",  client.status,
	 inet_ntoa(client.addr.sin_addr), ntohs(client.addr.sin_port));
  printf("InitSeq <Self : %u, Remote : %u>\n", client.seqno, client.ackno);

  FILE* file = fopen(argv[3], "rb");
  if( file == NULL ) {
    perror("fopen");
    return 1;
  }

  size_t filesize = get_filesize(argv[3]);
  if( dtp_send(&client, &filesize, sizeof(size_t)) != 0 ) {
    fprintf(stderr, "Error in dtp_send.\n");
    return 1;
  }

  printf("Sent file size : %lu\n", filesize);

  size_t chksize;
  if( dtp_recv(&client, &chksize, sizeof(size_t)) != 0 ) {
    fprintf(stderr, "Error in dtp_recv.\n");
    return 1;
  }

  printf("File size acked : %lu\n", chksize);

  const size_t BUFLEN = 5000;
  char buff[BUFLEN];
  while( 1 ) {
    size_t bytes = fread(buff, 1, BUFLEN, file);
    if( bytes == 0 )
      break;
    if( dtp_send(&client, buff, bytes) != 0 ) {
      fprintf(stderr, "Error in dtp_send.\n");
      return 1;
    }
  }

  fclose(file);

  if( dtp_recv(&client, &chksize, sizeof(size_t)) != 0 ) {
    fprintf(stderr, "Error in dtp_recv.\n");
    return 1;
  }

  printf("%s sent.\n", argv[3]);

  close_dtp_gate(&client);

  return 0;
}
