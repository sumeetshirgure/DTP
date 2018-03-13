#include "dtp.h"
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>

int main (int argc, char *argv[]) {
  if( argc != 3 ) {
    fprintf(stderr, "Usage: %s <server_ip> <server_port>\n", argv[0]);
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

  pause();

  return 0;
}
