#include "gate.h"

#include <string.h>		/* memset */
#include <arpa/inet.h>		/* inet_aton */

int init_dtp_server (dtp_server* server, port_t port_no) {
  int stat = 0;

  /* Create socket. */
  stat = (server->socket) = socket(AF_INET, SOCK_DGRAM, 0);
  if( stat < 0 )
    return -1;

  /* Create self socket address. */
  struct sockaddr_in* host = &(server->self);
  host->sin_family = AF_INET;
  host->sin_port = htons(port_no);
  host->sin_addr.s_addr = INADDR_ANY;
  memset(host->sin_zero, 0, sizeof(host->sin_zero));

  /* Bind to that address. */
  stat = bind(server->socket, (struct sockaddr*) host, sizeof(struct sockaddr_in));
  if( stat < 0 )
    return -1;

  /* Set socket options. */
  int optval = 1;
  stat = setsockopt(server->socket, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
		    (const void*) &optval, sizeof(int));
  if( stat < 0 )
    return -1;

  return 0;
}

int init_dtp_client (dtp_client* client, const char* hostname, port_t port_no) {
  int stat = 0;

  /* Create socket. */
  (client->socket) = socket(AF_INET, SOCK_DGRAM, 0);
  if( (client->socket) < 0 )
    return -1;

  /* Create remote server address. */
  struct sockaddr_in* host = &(client->addr);
  host->sin_family = AF_INET;
  host->sin_port = htons(port_no);
  inet_aton(hostname, &(host->sin_addr));
  memset(host->sin_zero, 0, sizeof(host->sin_zero));

  /* Set socket options. */
  int optval = 1;
  stat = setsockopt(client->socket, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
		    (const void*) &optval, sizeof(int));
  if( stat < 0 )
    return -1;

  return 0;
}
