#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

#include <netinet/in.h>
#include <netinet/tcp.h>


#define LISTEN_BACKLOG (1024)    // max length of listen backlog
/******************************************************************************\
|                                   Structs                                    |
\******************************************************************************/
typedef enum MsgType {
  MSG_NUL,
  MSG_IDENT,
  MSG_PASS,
  MSG_LOG
} MsgType;

typedef struct ConnState {
  struct pollfd  pfd;
  char           state;
  char*          name;
  char*          arg;
} ConnState;

typedef struct ConnMsg {
  MsgType          type;
  unsigned short   len;
  char*            body;
} ConnMsg;


/******************************************************************************\
|                             Forward Declarations                             |
\******************************************************************************/
int initClient(ConnState*);
int nameClient(ConnState*);
int authClient(ConnState*);
int mlogClient(ConnState*);
int termClient(ConnState*);
int servServer(ConnState*);
int errClient(ConnState*);


/******************************************************************************\
|                                State Machine                                 |
\******************************************************************************/
// Definition for the state machine
// This uses X-macros to ensure the state and enum definitions are synchronized
// at definition-time.  C99 designated initializers cover much of the same
// surface, but I still prefer X-macros for their entensibility.

// TODO I was in the middle of adding additional columns to designate state transition
!!!!!!!!!!!!!!!!!!!
#define EXPAND_ENUM(x,y) x,
#define EXPAND_FUNC(x,y) y,
#define STATE_TABLE(X) \
  X(SM_INIT, initClient, SM_) \
  X(SM_NAME, nameClient) \
  X(SM_AUTH, authClient) \
  X(SM_MLOG, mlogClient) \
  X(SM_TERM, termClient) \
  X(SM_SERV, servServer) \
  X(SM_ERR,  errClient)

int(* ConnMachine[])(ConnState*) = {
  STATE_TABLE(EXPAND_FUNC)
};

typedef enum SMState {
  STATE_TABLE(EXPAND_ENUM)
  SM_LEN
} SMState;

int Transitions[][2] = {


#define MAX_CLIENTS 8192
ConnState ConnTable[MAX_CLIENTS]      = {0};
struct pollfd* PollTable[MAX_CLIENTS] = {0}; // All fd members of ConnTable

int GetMsg(int fd, ConnMsg* connmsg) {
  // Drains a given file descriptor, casting the results into a ConnMsg type 


}

/******************************************************************************\
|                               State Functions                                |
\******************************************************************************/
int servServer(ConnState* connstate) {
  // Accepts a connection and promotes it to pending state in ConnTable.
  // Note that the client implementation shouldn't create multiple connections,
  // but there is nothing stopping a rogue client from doing so.  The common
  // defense would be to stash and check addr, but we do not do this.  We could
  // also throttle connections at auth-time based on name and a max_connections 
  // concept, but this solution is omitted here as well.
  //
  // Moreover, the server should only arrive here when the connection state is
  // SM_SERV.  This state cannot be achieved normally--it is a special state
  // designating listen-type sockets (currently only one).  We'll detect the
  // error and log it, but provide no means of recovery.
  int fd = accept(connstate->pfd.fd, NULL, NULL);
  if(-1==fd) {
    // TODO handle server error
  }
  ConnTable[fd].pfd.fd = fd;
  ConnTable[fd].state = SM_INIT; 

  // It should not be possible to receive a connection on a socket which had
  // state and did not clean it up.  We'll log this event in the hypothetical
  // scenario that continued development has happened and a bug was introduced. 
  if(ConnTable[fd].arg) {
    // TODO log this
    free(ConnTable[fd].arg);
    ConnTable[fd].arg = NULL;
  }
  if(ConnTable[fd].name) {
    // TODO log this
    free(ConnTable[fd].name);
    ConnTable[fd].name = NULL;
  }

  return SM_SERV;
}

int ServerInit(int port) {
  struct sockaddr_in sa = (struct sockaddr_in){0};
  int sockfd = -1;
  if(1>port || 65535<port) return -1;   // illegal port

  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;      // bind to all available
  if(-1==(sockfd=socket(PF_INET, SOCK_STREAM, IPPROTO_IP))) return -2;
  if(-1==bind(sockfd, &sa, sizeof(sa)))                     return -3;
  if(-1==listen(sockfd, LISTEN_BACKLOG))                    return -4;

  // We're listening.  Now initialize ConnTable and PollTable.
  for(int i=0; i<MAX_CLIENTS; i++) {
    ConnTable[i].pfd.fd = -1;
    ConnTable[i].pfd.events = POLLIN|POLLHUP; // ignored unless fd is positive
    PollTable[i] = &ConnTable.pfd;
  }

  return sockfd;
}

int ServerMainLoop(int listen_fd) {
  // Setup the listening connection state
  ConnTable[listen_fd].pfd.fd     = listen_fd;
  ConnTable[listen_fd].pfd.events = POLLIN;
  ConnTable[listen_fd].state      = SM_SERV;

  while((n=poll(PollTable, MAX_CLIENTS, -1))) {
    switch(n) {
      case EINTR:
        continue;
      case EFAULT:
      case EINVAL:
      case ENOMEM:
        return -1;
    }

    for(int fd=0; fd<MAX_CLIENTS; fd++) {
      if(POLLIN&PollTable[fd]->revents) {
        ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
      } else if(POLLHUP&PollTable[fd]->revents) {
        // TODO log disconnect notice
        // TODO implement disconnect
      } else {
        // TODO log weird status
      }

    }
  }
}


/******************************************************************************\
|                                 Entry Point                                  |
\******************************************************************************/
const int  default_port   = 5555;
const char default_file[] = "server.log";
int rc                    = -1;
int fd                    = -1;

int main(int argc, char** argv) {
  int port                = default_port;
  char file[FILENAME_MAX] = {0};
  if(argc>1) {
    int tmp_port = atoll(argv[1]);
    if(1>tmp_port || 65535<tmp_port) {
      printf("Port must be a valid integer between 1-65535\n");
      return -1;
    }
    port = tmp_port;
  }
  if(argc>2) {


  }

  if(0>(fd=ServerInit(port))) {
    printf("Could not bind to port %d because %s.\n", port, strerror(errno);
    return -1;
  }
  if((rc=ServerMainLoop(fd))) {
    printf("Error in main loop: %s\n", strerror(errno));
    return -1;
  }

}
