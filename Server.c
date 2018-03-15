#include "dtp.h"
#include <stdio.h>
#include <stdlib.h>

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

  pause();

  return 0;
}
