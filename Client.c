#include "dtp.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

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

  char text[1<<10];

  pthread_mutex_lock(&(client.inbuf_mtx));

  while( (client.inend + 1024 - client.inbeg)%1024 == 0 )
    pthread_cond_wait(&(client.inbuf_var), &(client.inbuf_mtx));
  size_t len = client.inbuf[client.inbeg].len;
  memcpy(text, client.inbuf[client.inbeg].data, len);
  client.inbeg = (client.inbeg + 1) % 1024;
  pthread_mutex_unlock(&(client.inbuf_mtx));

  text[len] = 0;

  printf("Received : [%s]\n", text);

  close_dtp_gate(&client);

  return 0;
}
