### Coding Exercise (Implementation)

This program implements a simple C server and corresponding Python client for
serializing messages to a file on the server machine.

## Server Implementation

./server [port]
port is an optional parameter (default: 5555), the desired TCP/IP port to bind
the server to.  It will bind on all available interfaces.

The server will listen to incoming connections.  Connections are serviced
through a state machine with the following transition diagram:

  C -> N -> P -> M -> M -> ... -> M -> X

  C - connection accepted by listening socket
  N - client supplies name
  P - client supplies password
  M - client supplies message
  X - client closes connection

At every state, the client must acknowledge that they understand the server
state machine by specifying the current state.  This is the first byte of every
transaction. Transaction payloads are preceeded by a two-byte length parameter
(not in network order, for simplicity!) which communicates the payload length in
bytes.  Payloads are interpreted relative to the current state, so the current
design has no need to consider payload types.

At any state, the client may explicitly transition to a closed state by:
 * emitting a zero-byte send() (the client might not do this intentfully, e.g.,
   by calling close() on the file descriptor and allowing their kernel's TCP/IP
   stack to close the connection, which will be interpreted as a zero-byte
   send() by the socket API on the server)
 * specifying a CLOSE transaction

It may also transition implicitly when the kernel's TCP timeout is triggered.
We do nothing to suppress or encourage keepalive frames, so this behavior is
dependent on system configuration.

Messages are appended to a message log in the CWD, ./messages.log

Transaction information is logged to ./transactions.log

## Client Implementation

The client implements the sending side of the protocol discussed above.  In
order to promote connection reuse, it also implements a connection table to
make it easy to interact with multiple servers without having to stash the
connection object.
