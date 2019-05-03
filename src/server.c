#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

#include <netinet/in.h>
#include <netinet/tcp.h>


/******************************************************************************\
|                              Utility Functions                               |
\******************************************************************************/
int FileInit(char* file) {
  return open(file, O_WRONLY|O_APPEND|O_CREAT, 0644);
}

// GCC has the format-printf __attribute__, which allows variadic functions to
// have compile-time printf-style type checks.  Never figured out whether the MS
// compiler has something similar,
void LogMe(int depth, const char *fmt, ...) {
  va_list arg;
  if(depth) printf("%*s", 4*depth, "");
  va_start(arg, fmt);
  vprintf(fmt, arg);
  printf("\n");
  va_end(arg);
}


/******************************************************************************\
|                    Connection and Message Data Structures                    |
\******************************************************************************/
typedef struct ConnState {
  struct pollfd*   pfd;
  char             state;
  unsigned short   arg_len;
  char*            name;
  char*            arg;
} ConnState;

typedef struct ConnMsg {
  char             type;
  unsigned short   len;
  char*            msg;
} ConnMsg;


/******************************************************************************\
|                          Forward State Declarations                          |
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
// This uses X-macros to ensure the state and enum definitions are synchronized
// at definition-time.  C99 designated initializers cover much of the same
// surface, but I still prefer X-macros for their extensibility.
#define MAX_CLIENTS      64    // Easy to make higher
#define BUF_MAX        4096
#define LISTEN_BACKLOG 1024    // max length of listen backlog
unsigned long msg_count = 0;   // running total of messages serviced
int fd_message = -1;           // file descriptor for message file

#define EXPAND_ENUM(a,b,c,d) a,
#define EXPAND_FUNC(a,b,c,d) b,
#define EXPAND_TRAN(a,b,c,d) {a, c, SM_TERM, SM_ERR},
#define EXPAND_NAME(a,b,c,d) #d ,
#define STATE_TABLE(X) \
  X(SM_INIT, initClient, SM_NAME, Init) \
  X(SM_NAME, nameClient, SM_AUTH, Name) \
  X(SM_AUTH, authClient, SM_MLOG, Pass) \
  X(SM_MLOG, mlogClient, SM_MLOG, Msg) \
  X(SM_TERM, termClient, SM_INIT, Term) \
  X(SM_SERV, servServer, SM_SERV, Serv) \
  X(SM_ERR,  errClient,  SM_ERR,  Error)

// State handlers
int(* ConnMachine[])(ConnState*) = {
  STATE_TABLE(EXPAND_FUNC)
};

// State types
typedef enum SMState {
  STATE_TABLE(EXPAND_ENUM)
  SM_LEN
} SMState;

// Transition types
typedef enum SMTrans {
  SMT_STAY,
  SMT_NEXT,
  SMT_CLOSE,
  SMT_ERR,
  SMT_LEN
} SMTrans;

// Transition matrix
int Transitions[][SMT_LEN] = {
  STATE_TABLE(EXPAND_TRAN)
};

// State names
char* StateNames[] = {
  STATE_TABLE(EXPAND_NAME)
};
const char* strconn(ConnState* connstate) {
  return StateNames[connstate->state];
}

// Lookup connection state by file descriptor.  In a full-featured application,
// this is undesirable because we may use file descriptors for other purposes
// than serving clients.  Here, we'll use the lookup without indirection.
ConnState ConnTable[MAX_CLIENTS]     = {0};
struct pollfd PollTable[MAX_CLIENTS] = {0};


/******************************************************************************\
|                        State Handler Helper Functions                        |
\******************************************************************************/
#define MSG_OK(fd)  send(fd, &(int){0},  1, MSG_NOSIGNAL);
#define MSG_BAD(fd) send(fd, &(int){-1}, 1, MSG_NOSIGNAL);
int GetMsg(int fd, ConnMsg* connmsg) {
  // Drains a given file descriptor, casting the results into a ConnMsg type
  // strongly assumes:
  //  * `fd` is set to O_NONBLOCK
  //  * the socket buffer holds a NEW message
  int m=0, n=0;
  SMState msg_state;
  unsigned short msg_len;
  static char buf[BUF_MAX] = {0};  // Allocating is slower than setting a page!
  memset(buf, 0, BUF_MAX);

  // Cleanup if there is data from last run
  connmsg->len = 0;
  if(connmsg->msg) {
    free(connmsg->msg);
    connmsg->msg = NULL;
  }

  // Get the type
  while(EINTR == (n=recv(fd, &msg_state, 1, 0))) ;
  if(1 != n) goto GETMSG_HANGUP;

  // It's possible that the client has sent the first two bytes of the header,
  // but not the third.  This would be exceedingly strange on modern systems
  // so we do not handle that case here.  But we should.
  while(EINTR == (n=recv(fd, &msg_len, 2, 0))) ;  // could be interrupted mid-sequence...
  if(2 != n) goto GETMSG_HANGUP;

  // We now know what kind of message we have.  Zero-byte messages should have
  // zero byte payloads.  Zero-byte messages cannot have zero-byte payloads.
  // nonzero messages cannot have zero payloads.
  switch(msg_state) {
    case SM_TERM:
    case SM_ERR:
      if(msg_len) goto GETMSG_HANGUP;
    default:
      if(!msg_len) goto GETMSG_HANGUP;
  }


  // Begin draining the message component.
  // TODO: protect against EINTR
  connmsg->msg = calloc(1, msg_len+1);
  n = 0;
  while(0<msg_len - n) {
    n+=(m=recv(fd, connmsg->msg+n, msg_len-n, 0));

    // Zero-byte return means the client hung up.  Zero-byte on O_NONBLOCK gives
    // -1.  We do not accept partial messages.
    if(0==m) goto GETMSG_HANGUP;
  }

  connmsg->type = msg_state;
  connmsg->len  = msg_len;
  return 0;

GETMSG_HANGUP:
  // We should only be here in the case that the socket operations are in error
  if(connmsg->msg) {
    free(connmsg->msg);
    connmsg->msg = NULL;
  }
  connmsg->type = SM_ERR;
  connmsg->len  = 0;
  return -1;
}


/******************************************************************************\
|                           State Handler Functions                            |
\******************************************************************************/
int initClient(ConnState* connstate) {
  int fd = connstate->pfd->fd;
  int flags = 0;

  // It should not be possible to receive a connection on a socket which had
  // state and did not clean it up.  We'll log this event in the hypothetical
  // scenario that continued development has happened and a bug was introduced.
  if(ConnTable[fd].arg) {
    LogMe(1, "Superfluous arg cleanup on %d (%s).",fd, strconn(connstate));
    free(ConnTable[fd].arg);
    ConnTable[fd].arg = NULL;
  }
  if(ConnTable[fd].name) {
    LogMe(1, "Superfluous name cleanup on %d (%s).",fd, strconn(connstate));
    free(ConnTable[fd].name);
    ConnTable[fd].name = NULL;
  }

  // Set new connection to nonblocking.
  if(-1==(flags=fcntl(fd, F_GETFL, 0))) {
    LogMe(1, "Error getting fcntl() status on %d.  Not doing anything.", fd);
  }
  if(-1==fcntl(fd, F_SETFL, O_NONBLOCK|flags)) {
    LogMe(1, "Error setting fcntl() status on %d.  Not doing anything.", fd);
  }

  return SMT_NEXT;
}

int nameClient(ConnState* connstate) {
  // If we had a list of valid clients, we may choose to do some pre-auth logic
  // here, such as prepping the password or engaging SSO.  As it stands, we're
  // just waiting until the auth request hits us.
  if(0==connstate->arg_len || 0==connstate->arg[0]) {
    return SMT_ERR;
  }
  if(connstate->name) {
    // Standard disclaimer.  We're doing this defensively.
    LogMe(1, "Superfluous name cleanup on %d (%s).", connstate->pfd->fd,
                                                     strconn(connstate));
    free(connstate->name);
    connstate->name = NULL;
  }

  connstate->name = calloc(1, 1+connstate->arg_len);
  memcpy(connstate->name, connstate->arg, connstate->arg_len);

  // Everything is fine, so acknowledge with client
  MSG_OK(connstate->pfd->fd);
  return SMT_NEXT;
}

int authClient(ConnState* connstate) {
  // Uh.  Well, we could manage a list of users and their passwords, or we could
  // just check whether the first character is odd.
  if(0==connstate->arg_len) {
    // Can't have null passwords
    return SMT_ERR;
  }

  if(!connstate->arg[0]&1) return SMT_ERR;       // nope!
  MSG_OK(connstate->pfd->fd);
  return connstate->arg[0]&1 ? SMT_NEXT : SM_ERR;
}

int mlogClient(ConnState* connstate) {
  // We write down the message.  It might be reasonable to expect that the
  // message is ASCII and we ought to do some kind of escaping, but we don't
  // do that--treat as binary.
  if(0==connstate->arg_len) return SMT_ERR;  // no null messages.

  // Prepare message header.  If the name contains null-bytes, obviously it'll
  // be truncated here.  We don't care (if it hurts, stop doing it).
  char header[BUF_MAX+5] = {0};
  int n;
  n = snprintf(header, BUF_MAX+5, "[%s]: ", connstate->name);

  // Write the header, the msg, and a terminating CR+LF (this is Windows!)
  while(EINTR==(n=write(fd_message, header, n))) ;
  if(0>n) goto MLOG_HANGUP;
  while(EINTR==(n=write(fd_message, connstate->arg, connstate->arg_len))) ;
  if(0>n) goto MLOG_HANGUP;
  while(EINTR==(n=write(fd_message, "\r\n", 2))) ;
  if(0>n) goto MLOG_HANGUP;

  // Everything is fine.  Let the client know.
  MSG_OK(connstate->pfd->fd);
  return SMT_NEXT;


MLOG_HANGUP:
  // We hangup on the client to let them know that something went wrong on this
  // end and we can no longer service their messages.
  return SMT_ERR;
}

void ClientCleanupHelper(ConnState* connstate) {
  connstate->pfd->fd = -1;
  connstate->arg_len = 0;
  if(connstate->arg) {
    free(connstate->arg);
    connstate->arg = NULL;
  }
  if(connstate->name) {
    free(connstate->name);
    connstate->name = NULL;
  }
}

int termClient(ConnState* connstate) {
  MSG_OK(connstate->pfd->fd);
  close(connstate->pfd->fd);
  ClientCleanupHelper(connstate);

  return SMT_NEXT;
}

int errClient(ConnState* connstate) {
  MSG_BAD(connstate->pfd->fd);
  close(connstate->pfd->fd);
  ClientCleanupHelper(connstate);
  return SMT_NEXT;
}

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
  //
  // Finally, because of the way we use file descriptors as indices, we need to
  // dump any connections that are numbered too high.  File descriptors are
  // allocated by the kernel from lowest-available-first, so we shouldn't have
  // huge gaps.
  int fd = -1, flags = 0;
  SMTrans trans;
  fd = accept(connstate->pfd->fd, NULL, NULL);
  if(-1==fd) {
    LogMe(0, "Error on accept().  Not doing anything.");
  } else if(MAX_CLIENTS<=fd) {
    LogMe(0, "Client limit hit!  Requested fd: %d, max: %d", fd, MAX_CLIENTS);
    close(fd);
  }

  ConnTable[fd].pfd->fd = fd;
  ConnTable[fd].state = SM_INIT;

  // Actually run the init transaction.  If it succeeds, transition to the next
  // state
  trans = ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
  ConnTable[fd].state = Transitions[ConnTable[fd].state][trans];

  return SMT_STAY;
}


/******************************************************************************\
|                               Server Functions                               |
\******************************************************************************/
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
    ConnTable[i].pfd = &PollTable[i];
    ConnTable[i].pfd->fd = -1;
    ConnTable[i].pfd->events = POLLIN|POLLHUP; // ignored unless fd is positive
  }

  return sockfd;
}

int ServerMainLoop(int listen_fd) {
  ConnMsg msg = {0};
  SMTrans trans;
  int n = -1;
  LogMe(0,"Listening for new connections...");

  // Setup the listening connection state
  ConnTable[listen_fd].pfd->fd     = listen_fd;
  ConnTable[listen_fd].pfd->events = POLLIN;
  ConnTable[listen_fd].state       = SM_SERV;

  while((n=poll(PollTable, MAX_CLIENTS, -1))) {
    if(-1==n) switch(errno) {
      case EINTR:
        continue;
      case EFAULT:
      case EINVAL:
      case ENOMEM:
        return -1;
    }

    for(int fd=0; fd<MAX_CLIENTS; fd++) {
      if(POLLIN&PollTable[fd].revents) {
        LogMe(0, "[%d](%s):", fd, strconn(&ConnTable[fd]));

        // Proceed to read from the file descriptor.  Note that we early-exit
        // from the first if() when it's a server listening FD, since we do not
        // want to try to recv() the connection request
        if(SM_SERV == ConnTable[fd].state) {
          // We special-case the server socket(s).
          trans = ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
          ConnTable[fd].state = Transitions[ConnTable[fd].state][trans];
        } else if(GetMsg(fd, &msg)) {
          // Error was thrown in GetMsg!  Let the client know that there was
          // an error and disconnect.
          LogMe(1, "Error deserializing message (or client hung up)");
          ConnTable[fd].state = SM_ERR;
        } else if(msg.type != ConnTable[fd].state) {
          // This is an illegal transaction!  Let the client know that there
          // was an error and disconnect.
          LogMe(1, "Client desynchronized (%s)", StateNames[msg.type]);
          ConnTable[fd].state = SM_ERR;
        } else {
          // We're fine, process the transaction.
          if(msg.len) {
            LogMe(1, "value: %s", msg.msg);
          } else {
            LogMe(1, "Message deserialized.  Processing transaction.");
          }
          msg_count++;
          ConnTable[fd].arg     = msg.msg;
          ConnTable[fd].arg_len = msg.len;
          trans = ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
          ConnTable[fd].state = Transitions[ConnTable[fd].state][trans];
        }

        // We're done with the msg/arg, so throw it away.
        if(msg.msg) free(msg.msg);
        msg.msg           = NULL;
        ConnTable[fd].arg = NULL;

        // If the state type is SM_TERM or SM_ERR, then we need to process that
        // before returning to the main loop
        // ASSERT:  these two transactions will close the connection and clean
        //          up the ConnState object (e.g., return it to an init state)
        if(SM_ERR == ConnTable[fd].state || SM_TERM == ConnTable[fd].state) {
          LogMe(1, "Encountered an error.  Closing.");
          ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
        }
      } else if(POLLHUP&PollTable[fd].revents) {
        LogMe(0, "[%d](%s): HANGUP", fd, strconn(&ConnTable[fd]));
        errClient(&ConnTable[fd]);
        ConnTable[fd].state = SM_INIT;
      } else if(PollTable[fd].revents) {
        LogMe(0, "[%d](%s): Unexpected", fd, strconn(&ConnTable[fd]));
        errClient(&ConnTable[fd]);
        ConnTable[fd].state = SM_INIT;
      }
    }
  }
}


/******************************************************************************\
|                                 Entry Point                                  |
\******************************************************************************/
int main(int argc, char** argv) {
  int port                  = default_port;
  int rc                    = -1;
  int sfd                   = -1;  // file descriptor for sockets
  const int  default_port   = 5555;
  const char default_file[] = "messages.log";
  if(argc>1) {
    int tmp_port = atoll(argv[1]);
    if(1>tmp_port || 65535<tmp_port) {
      printf("Port must be a positive integer less than 65535\n");
      return -1;
    }
    port = tmp_port;
  }

  // We don't clean up after the errors below, since exit() handles that for us
  if(0>(sfd=ServerInit(port))) {
    printf("Could not bind to port %d because %s.\n", port, strerror(errno));
    return -1;
  }
  else if(0>(fd_message=FileInit(default_file))) {
    // Can't really log this, so we just yell.
    printf("Could not open file %s because %s\n", default_file, strerror(errno));
    return -1;
  } else if((rc=ServerMainLoop(sfd))) {
    printf("Error in main loop: %s\n", strerror(errno));
    return -1;
  }

  // This won't be reachable until the server is allowed to shut down
  // gracefully.  This is not implemented because I don't want to deal with
  // learning how to switch Windows consoles to raw input in the correct way in
  // order to hook it up to the poll() loop (I don't want to use timeouts...)
  printf("Server closing.  I served %ld transactions!  Goodbye.\n", msg_count);

}
