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

  char text[1<<7];
  scanf("%[^\n]", text); getchar();
  ssize_t texlen = strlen(text);

  pthread_mutex_lock(&(server.outbuf_mtx));

  make_pkt(&(server.outbuf[server.outend]),
	   server.seqno,
	   0,
	   server.outend,
	   texlen,
	   1024,
	   0,
	   text);
  server.outbeg = (server.outbeg + 1)%1024;

  pthread_mutex_unlock(&(server.outbuf_mtx));

  close_dtp_gate(&server);

  return 0;
}
