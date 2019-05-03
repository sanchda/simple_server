#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

int main(void) {
  struct sockaddr_in serv;
  serv.sin_family = AF_INET;
  serv.sin_port = htons(5555);
  inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  connect(3, &serv, sizeof(serv));

  // Send NAME transaction
  send(3,"\1",1,0);
  send(3,"\4\0", 2, 0);
  send(3,"Name",4,0);

  // Send Identification transaction
  send(3,"\2", 1, 0);
  send(3,"\1\0", 2, 0);
  send(3,"\1", 1, 0);

  // Send MSG transaction
  send(3,"\3", 1, 0);
  send(3,"\5\0", 2, 0);
  send(3,"Hello", 5, 0);

  return 0;
}
