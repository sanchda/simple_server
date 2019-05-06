#define _CRT_SECURE_NO_WARNINGS  // Only because non-prod code...

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>


#if defined(__MINGW32__)    // If using W32-GCC
  #include <unistd.h>
#elif defined(_WIN32)       // Using MSVC
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
#else                       // Using Linux-GCC
  #include <unistd.h>
  #include <sys/socket.h>
  #include <poll.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
#endif


/******************************************************************************\
|                           Utility / Compatibility                            |
\******************************************************************************/
int FileInit(char* file) {
  return open(file, O_WRONLY | O_APPEND | O_CREAT, 0644);
}

void LogMe(int depth, const char* fmt, ...) {
  va_list arg;
  if(depth) printf("%*s", 4 * depth, "");
  va_start(arg, fmt);
  vprintf(fmt, arg);
  printf("\n");
  va_end(arg);
}

char SafeRecv(int fd, unsigned char* buf, unsigned int len) {
  // Returns 0 if exactly len bytes were extracted from the socket, otherwise
  // returns 1 (e.g., hangup, error, or fewer bytes)
  int recvd = 0, rv = 0;  // total received, last return value
  while(0 < len - recvd) {
    recvd += (rv = recv(fd, buf + recvd, len - recvd, 0));
    if(-1 == rv && EINTR == errno) continue;     // retry if interrupted
    else if(0 >= rv)               return 1;     // hangup or error
  }
  return 0;
}

char SafeWrite(int fd, char* buf, unsigned int len) {
  int wrote = 0, rv = 0;  // total written, last return value
  while(0 < len - wrote) {
    wrote += (rv = write(fd, buf + wrote, len - wrote));
    if(-1 == rv && EINTR == errno) continue;
    else if(0 >= rv)               return 1;
  }
  return 0;
}

// Thanks mingw! (taken from Windows 10's winsock2.h, ymmv but these appear to
// be stable since their introduction in Vista)
#ifdef __MINGW32__
  #define POLLRDNORM  0x0100
  #define POLLRDBAND  0x0200
  #define POLLIN      (POLLRDNORM | POLLRDBAND)
  typedef struct pollfd {
    SOCKET  fd;
    SHORT   events;
    SHORT   revents;
  } WSAPOLLFD, * PWSAPOLLFD, FAR* LPWSAPOLLFD;
#endif

#ifdef _WIN32
  #define MSG_NOSIGNAL 0 // Not needed on Windows
  #define poll WSAPoll
  #define BADSOCK (~0)   // wsock returns are unsigned
#else
  #define BADSOCK (-1)
#endif

char SetSocketNonBlocking(int fd) {
  if(fd < 0) return 1;
#ifdef _WIN32
  return ioctlsocket(fd, FIONBIO, &(int){1});
#else
  int flags = 0;
  if(-1 == (flags = fcntl(fd, F_GETFL, 0)))        return 1;
  if(-1 == fcntl(fd, F_SETFL, O_NONBLOCK | flags)) return 1;
  return 0;
#endif
}

/******************************************************************************\
|                    Connection and Message Data Structures                    |
\******************************************************************************/
typedef struct ConnState {
  struct pollfd* pfd;
  char             state;
  unsigned short   arg_len;
  char* name;
  char* arg;
} ConnState;

typedef struct ConnMsg {
  char             type;
  unsigned short   len;
  char* msg;
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
\***************************************************************************** /
This is a state-machine based solution, so this section defines the most
critical parts of the application.

Instead of defining a state-to-state transition matrix, since most states have a
logical "next" state, I define a state-to-transition_type matrix.  SM_MLOG,
SM_SERV are steady-states. The SM_ERR and SM_TERM states are sinks,returning the
connection back to unused.

Note that while SM_INIT is steady, there is to init executor--a connection must
be lifted to SM_NAME by the server at connect-time.  This might feel weird,
unless you've been haunted by a desire for a connection-open handler in a
previous protocol implementation.

In particular:
  ConnState - per-connection metadata.  Indexed by file descriptor (incl listen)
  PollTable - array of pollfd for poll().  fd = -1 when ignored ("init state")
  ConnMachine - array of handler functions for the connection states
  SMState - state enum
  SMTrans - state transition enum
  Transitions - given a state and a transition type, return the output state
  Immediate, Talkative, Lightweight - state attributes
*/
#ifdef _WIN32
  #define MAX_CLIENTS  8192
#else
  #define MAX_CLIENTS    64
#endif
#define BUF_MAX        4096
#define LISTEN_BACKLOG 1024    // max length of listen backlog
unsigned long msg_count = 0;   // running total of messages serviced
int fd_message = -1;           // file descriptor for message file

// Define state transitions with X-macro
#define EXPAND_ENUM(a,b,c,d,e,f,g) a,
#define EXPAND_FUNC(a,b,c,d,e,f,g) b,
#define EXPAND_TRAN(a,b,c,d,e,f,g) {a, c, SM_TERM, SM_ERR},
#define EXPAND_NAME(a,b,c,d,e,f,g) #d ,
#define EXPAND_IMMD(a,b,c,d,e,f,g) e,
#define EXPAND_TALK(a,b,c,d,e,f,g) f,
#define EXPAND_LTWT(a,b,c,d,e,f,g) g,
#define STATE_TABLE(X) \
  X(SM_INIT, initClient, SM_NAME, Init,  0, 0, 0) \
  X(SM_NAME, nameClient, SM_AUTH, Name,  0, 1, 0) \
  X(SM_AUTH, authClient, SM_MLOG, Pass,  0, 1, 0) \
  X(SM_MLOG, mlogClient, SM_MLOG, Msg,   0, 1, 0) \
  X(SM_TERM, termClient, SM_INIT, Term,  0, 0, 0) \
  X(SM_SERV, servServer, SM_SERV, Serv,  0, 0, 0) \
  X(SM_ERR,  errClient,  SM_INIT, Error, 1, 0, 0)

// State handlers
int(*ConnMachine[])(ConnState*) = {
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
const int Transitions[][SMT_LEN] = {
  STATE_TABLE(EXPAND_TRAN)
};

// State names
const char* StateNames[] = {
  STATE_TABLE(EXPAND_NAME)
};
const char* strconn(ConnState * connstate) {
  return StateNames[connstate->state];
}

// Immediate states
// These are states which must be computed immediately.  It is strongly assumed
// that the state machine cannot enter an unbreakable cycle of immediate states.
const char Immediate[] = {
  STATE_TABLE(EXPAND_IMMD)
};

// Talkative states
// These are states which are expected to receive a message from the client.
// This is important because accepting a connection does not drain the recv()
// buffer.
const char Talkative[] = {
  STATE_TABLE(EXPAND_TALK)
};

// Lightweight states
// These are states for which the message does not have a payload.
const char Lightweight[] = {
  STATE_TABLE(EXPAND_LTWT)
};

// Lookup connection state by file descriptor.  In a full-featured application,
// this is undesirable because we may use file descriptors for other purposes
// than serving clients.  Here, we'll use the lookup without indirection.
ConnState ConnTable[MAX_CLIENTS]     = {0};  // Incomplete on MSVC
struct pollfd PollTable[MAX_CLIENTS] = {0};


/******************************************************************************\
|                        State Handler Helper Functions                        |
\******************************************************************************/
#define MSG_OK(fd)  send(fd, &(int){0},  1, MSG_NOSIGNAL);
#define MSG_BAD(fd) send(fd, &(int){-1}, 1, MSG_NOSIGNAL);

int GetMsg(int fd, ConnMsg * connmsg) {
  // Drains a given file descriptor, casting the results into a ConnMsg type
  unsigned char msg_state;
  unsigned short msg_len;
  static char buf[BUF_MAX] = { 0 };  // Allocating is slower than setting a page!
  memset(buf, 0, BUF_MAX);

  // Cleanup if there is data from last run
  connmsg->len = 0;
  if(connmsg->msg) {
    free(connmsg->msg);
    connmsg->msg = NULL;
  }

  // Extract necessary parts of the message.  If we experience any issues, then
  // hangup the connection.
  if(SafeRecv(fd, &msg_state, 1) ||
    SafeRecv(fd, &msg_len, 2) ||
    !msg_len == !Lightweight[msg_state]) goto GETMSG_HANGUP;

  // Get the message body (if msg_len is zero, that is no-op)
  if(0 < msg_len) {
    connmsg->msg = calloc(1, msg_len + 1);
    if(SafeRecv(fd, connmsg->msg, msg_len)) goto GETMSG_HANGUP;
  }
  connmsg->type = msg_state;
  connmsg->len = msg_len;
  return 0;

GETMSG_HANGUP:
  if(connmsg->msg) {
    free(connmsg->msg);
    connmsg->msg = NULL;
  }
  connmsg->type = SM_ERR;
  connmsg->len = 0;
  return -1;
}

// This basically hardcodes a state-state transition matrix, which we admit
// since the protocol is so simple.
char IsInvalidState(unsigned int msgState, unsigned int connState) {
  if(msgState == connState) return 0;
  if(msgState == SM_TERM)   return 0;
  return 1;
}

void ClientCleanupHelper(ConnState * connstate) {
  connstate->pfd->fd     = -1;
  connstate->pfd->events = 0;
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


/******************************************************************************\
|                           State Handler Functions                            |
\******************************************************************************/
int initClient(ConnState * connstate) {
  int fd = connstate->pfd->fd;
  int flags = 0;

  // It should not be possible to receive a connection on a socket which had
  // state and did not clean it up.  We'll log this event in the hypothetical
  // scenario that continued development has happened and a bug was introduced.
  if(ConnTable[fd].arg) {
    LogMe(1, "Superfluous arg cleanup on %d (%s).", fd, strconn(connstate));
    free(ConnTable[fd].arg);
    ConnTable[fd].arg = NULL;
  }
  if(ConnTable[fd].name) {
    LogMe(1, "Superfluous name cleanup on %d (%s).", fd, strconn(connstate));
    free(ConnTable[fd].name);
    ConnTable[fd].name = NULL;
  }

  // Set new connection to nonblocking.
  if(SetSocketNonBlocking(fd))
    LogMe(1, "Error setting %d to non-blocking.  Not doing anything.", fd);

  return SMT_NEXT;
}

int nameClient(ConnState * connstate) {
  if(0 == connstate->arg_len || 0 == connstate->arg[0]) {
    // Need non-null name
    return SMT_ERR;
  }
  if(connstate->name) {  // Defensively.
    LogMe(1, "Superfluous name cleanup on %d (%s).", connstate->pfd->fd,
      strconn(connstate));
    free(connstate->name);
    connstate->name = NULL;
  }

  connstate->name = calloc(1, 1 + connstate->arg_len);
  memcpy(connstate->name, connstate->arg, connstate->arg_len);

  // Everything is fine, so acknowledge with client
  MSG_OK(connstate->pfd->fd);
  return SMT_NEXT;
}

int authClient(ConnState * connstate) {
  // Uh.  Well, we could manage a list of users and their passwords, or we could
  // just check whether the first character is odd.
  if(0 == connstate->arg_len) {
    LogMe(1, "Given empty password");
    return SMT_ERR;
  }

  if(!(connstate->arg[0] & 1)) {
    LogMe(1, "Invalid password");
    return SMT_ERR;       // nope!
  } else {
    LogMe(1, "Client connected successfully");
    MSG_OK(connstate->pfd->fd);
  }
  return SMT_NEXT;
}

int mlogClient(ConnState * connstate) {
  if(0 == connstate->arg_len) return SMT_ERR;  // no null messages.

  // Prepare message header. Names may be truncated if they contain nulls. (or
  // display in a weird way)
  char header[BUF_MAX + 5] = { 0 };
  int n;
  n = snprintf(header, BUF_MAX + 5, "[%s]: ", connstate->name);

  // Write the header, the msg, and a terminating CR+LF (this is Windows!)
  if(SafeWrite(fd_message, header, n) ||
     SafeWrite(fd_message, connstate->arg, connstate->arg_len) ||
     SafeWrite(fd_message, "\r\n", 2)) goto MLOG_HANGUP;

  // Everything is fine.  Let the client know.
  MSG_OK(connstate->pfd->fd);
  return SMT_NEXT;


MLOG_HANGUP:
  return SMT_ERR;
}

int termClient(ConnState * connstate) {
  MSG_OK(connstate->pfd->fd);
  close(connstate->pfd->fd);
  ClientCleanupHelper(connstate);

  return SMT_NEXT;
}

int errClient(ConnState * connstate) {
  MSG_BAD(connstate->pfd->fd);
#ifdef _WIN32
  closesocket(connstate->pfd->fd);
#else
  close(connstate->pfd->fd);
#endif
  ClientCleanupHelper(connstate);
  return SMT_NEXT;
}

int servServer(ConnState * connstate) {
  int fd = -1, flags = 0;
  SMTrans trans;
  fd = accept(connstate->pfd->fd, NULL, NULL);
  if(BADSOCK == fd) {
    LogMe(1, "Error on accept().  Not doing anything.");
  }
  else if(MAX_CLIENTS <= fd) {
    LogMe(1, "Client limit hit!  Requested fd: %d, max: %d", fd, MAX_CLIENTS);
    close(fd);
  }

  ConnTable[fd].pfd->fd     = fd;
  ConnTable[fd].pfd->events = POLLIN;
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
  struct sockaddr_in sa = (struct sockaddr_in) { 0 };
  int sockfd = -1;
  if(1 > port || 65535 < port) return -1;   // illegal port

#ifdef _WIN32
  WORD req = MAKEWORD(2, 2);
  WSADATA wsadata = {0};
  int err = WSAStartup(req, &wsadata);
  if(err) return -1;
#endif
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;      // bind to all available
  if(BADSOCK == (sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) ||
     BADSOCK == bind(sockfd, &sa, sizeof(sa))                       ||
     BADSOCK == listen(sockfd, LISTEN_BACKLOG)) {
#ifdef _WIN32
    errno = WSAGetLastError();
#endif
    return -1;
  }

       // We're listening.  Now initialize ConnTable and PollTable.
  for(int i = 0; i < MAX_CLIENTS; i++) {
    ConnTable[i].pfd = &PollTable[i];
    ConnTable[i].pfd->fd = -1;
    ConnTable[i].pfd->events = 0;
    ConnTable[i].name = NULL;
    ConnTable[i].arg  = NULL;
    ConnTable[i].arg_len = 0;
  }

  // Setup the listening connection state
  ConnTable[sockfd].pfd->fd = sockfd;
  ConnTable[sockfd].pfd->events = POLLIN;
  ConnTable[sockfd].state = SM_SERV;

  return sockfd;
}

int ServerMainLoop() {
  // Implements the main loop.  Waits until a socket is ready.  If input, then
  // use Talkative lookup to determine whether to recv() off the socket (this
  // is a kludge to let SM_SERV in the same loop without a special case).  Check
  // msg for valid state (if msg err or invalid state, hangup).  Finally, use 
  // msg state to run state handler on connection, updating connection state.
  ConnMsg msg = { 0 };
  SMTrans trans;
  int n = -1;
  LogMe(0, "Listening for new connections...");

  while((n = poll(PollTable, MAX_CLIENTS, -1))) {
    if(-1 == n && EINTR == errno) continue;
    else if(-1 == n)              return -1;

    // Note double exit criteria: most of the time we will have only one socket
    // ready, and it will be low-numbered.  Large-scale solution would employ a
    // different polling mechanism.  Ignore STDIO
    for(int fd = 3; fd < MAX_CLIENTS && n>0; fd++) {
      if(!(PollTable[fd].revents & PollTable[fd].events))
        continue; //Skip

      n--; // Not skipped, count down
      LogMe(0, "[%d](%s):", fd, strconn(&ConnTable[fd]));
      if(!(POLLIN & PollTable[fd].revents)) {
         LogMe(1, "Unexpected socket state.  Disconnecting client.");
         ConnTable[fd].state = SM_ERR;
      }


      // Evaluate until the current state is no longer Immediate
      do {
        // If this is a talkative state, we can get a message from the client.
        if(Talkative[ConnTable[fd].state] &&
           GetMsg(fd, &msg) &&
           IsInvalidState(msg.type, ConnTable[fd].state)) {

          LogMe(1, "Error or close.  Hanging up on client.");
          ConnTable[fd].state = SM_ERR;
        }

        if(Talkative[ConnTable[fd].state])
          LogMe(1, "Got message: %s", msg.msg);

        msg_count++;
        ConnTable[fd].arg = msg.msg;
        ConnTable[fd].arg_len = msg.len;
        trans = ConnMachine[ConnTable[fd].state](&ConnTable[fd]);
        ConnTable[fd].state = Transitions[ConnTable[fd].state][trans];
        if(msg.msg) free(msg.msg);
        msg.msg = NULL;
        ConnTable[fd].arg = NULL;
      } while(Immediate[ConnTable[fd].state]);
    }
  }
}


/******************************************************************************\
|                                 Entry Point                                  |
\******************************************************************************/
// Note that we leave cleanup for one-time objects, such as the listen socket,
// to the OS through exit()
int main(int argc, char** argv) {
  char file[] = "messages.log";
  int port = 5555;
  LogMe(0, "I'm up.");
  if(argc > 1) {
    int tmp_port = atoll(argv[1]);
    if(1 > tmp_port || 65535 < tmp_port) {
      LogMe(0, "Port must be a positive integer less than 65535");
      return -1;
    }
    port = tmp_port;
  }

  if(0 > ServerInit(port)) {
    LogMe(0, "Could not bind to port %d because %s.", port, strerror(errno));
    return -1;
  }
  else if(0 > (fd_message = FileInit(file))) {
    LogMe(0, "Could not open file %s because %s", file, strerror(errno));
    return -1;
  }
  return ServerMainLoop();
}
