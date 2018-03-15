#include "dtp.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

int main (int argc, char *argv[]) {
  if( argc != 2 ) {
    fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
    return 1;
  }

  char client_ip[1<<5];
  port_t client_port, server_port = atoi(argv[1]);
  int stat;
  dtp_server server;
  stat = init_dtp_server(&server, server_port);
  if( stat < 0 ) {
    perror("Init client :");
    return 1;
  }
  printf("Server listening on port %u\n", ntohs(server.self.sin_port));

  stat = dtp_listen(&server, client_ip, &client_port);
  if( stat < 0 ) {
    perror("Connect :");
    return 1;
  }

  printf("(%d) :: %s:%u\n",  server.status, client_ip, client_port);
  printf("InitSeq <Self : %u, Remote : %u>\n", server.seqno, server.ackno);

  size_t filesize;
  if( dtp_recv(&server, &filesize, sizeof(size_t)) != 0 ) {
    fprintf(stderr, "Error in dtp_recv.\n");
    return 1;
  }

  printf("Received file size : %lu\n", filesize);

  if( dtp_send(&server, &filesize, sizeof(size_t)) != 0 ) {
    fprintf(stderr, "Error in dtp_send.\n");
    return 1;
  }

  size_t remsize = filesize;
  FILE* outfile = fopen("Outfile", "wb");

  const size_t BUFLEN = 1<<10;
  char buff[BUFLEN];
  while( remsize > 0 ) {
    size_t len = remsize;
    if( len > BUFLEN ) len = BUFLEN;
    if( dtp_recv(&server, buff, len) != 0 ) {
      fprintf(stderr, "Error in dtp_recv.\n");
      return 1;
    }
    fwrite(buff, 1, len, outfile);
    remsize -= len;
    printf("Bytes remaining :\t%10lu\r", remsize);
    fflush(stdout);
  }

  printf("\nOutfile received.\n");

  fclose(outfile);

  close_dtp_gate(&server);

  return 0;
}
