#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

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
  int     fd;
  FILE*   fstream;
  char    state;
} ConnState;

typedef struct ConnMsg {
  MsgType   type;
  long      len;
  char*     body;
} ConnMsg;


/******************************************************************************\
|                             Forward Declarations                             |
\******************************************************************************/
int initClient(ConnState* connstate);
int nameClient(ConnState* connstate);
int authClient(ConnState* connstate);
int mlogClient(ConnState* connstate);
int termClient(ConnState* connstate);
int errClient(ConnState*  connstate);

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

  return sockfd;
}

int ServerMainLoop(int fd) {

}


/******************************************************************************\
|                                State Machine                                 |
\******************************************************************************/
// Definition for the state machine
// This uses X-macros to ensure the state and enum definitions are synchronized
// at definition-time.  C99 designated initializers cover much of the same
// surface, but I still prefer X-macros for their entensibility.
#define EXPAND_ENUM(x,y) x,
#define EXPAND_FUNC(x,y) y,
#define STATE_TABLE(X) \
  X(SM_INIT, initClient) \
  X(SM_NAME, nameClient) \
  X(SM_AUTH, authClient) \
  X(SM_MLOG, mlogClient) \
  X(SM_TERM, termClient) \
  X(SM_ERR,  errClient)

typedef enum SState {
  STATE_TABLE(EXPAND_ENUM)
  SM_LEN
} SState;

int(* cstates[])(ConnState*) = {
  STATE_TABLE(EXPAND_FUNC)
};


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
