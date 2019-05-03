### Coding Exercise (Implementation)

This program implements a simple C server and corresponding Python client for
serializing messages to a file on the server machine.


## Server Directions 

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

The server will respond with a single-byte indicating whether the transition
failed (-1) or succeeded (0).  It doesn't really matter, but close transactions
are confirmed.

At any state, the client may explicitly transition to a closed state by:
 * emitting a zero-byte send() (the client might not do this intentfully, e.g.,
   by calling close() on the file descriptor and allowing their kernel's TCP/IP
   stack to close the connection, which will be interpreted as a zero-byte
   send() by the socket API on the server)
 * specifying a CLOSE transaction

It may also transition implicitly when the kernel's TCP timeout is triggered.
We do nothing to suppress or encourage keepalive frames, so this behavior is
dependent on system configuration.

Messages are appended to a message log in the CWD, ./messages.log.  Certain
transaction diagnostics are printed to stdout.


## Client Directions 

The client implements the sending side of the protocol discussed above.  Usage
is pretty simple.  Initializing a client object requires setting the username
and password:

```python
import client

myclient = client.client("MyUser", "MyPassword")

```

Note that the client transmits the username and password as ASCII--binary data
is not currently supported by the client (although binary messages are supported
by the server).

Once a client has been connected, it is free to submit messages:
```python
myclient.msg("This is my message.  It is modestly important.")
myclient.msg("I can send multiple messages without reconnecting.")

# Oops, gotta go
myclient.disconnect()

# OK, I'm back now.
myclient.connect()
```

The client object makes no limit on the number of independent sessions which are
spawned.
```python
foo = client.client("First User", "C")
bar = client.client("Second User", "E")
```

Finally, note that passwords are very weak.  The parity of the ordinal value of
the first password character is all that matters.



## Design Considerations

This pattern is probably overkill for the nature of the exercise, but I didn't
feel that a quick-and-dirty solution would adequately represent my skills or
experiences.  I hope the team finds the additional burden of parsing through a
more complex solution worthwhile.

From an ergonomics standpoint, this protocol is a bit challenging to use.  This
is intentional, as it provided a simple way of assuring that a malicious client
couldn't execute arbitrary transactions.  In particular, I wanted to protect
against:
 * Message deserialization buffer overflows
 * Incomplete constraint of protocol state-machine

Moreover, whenever you implement a state machine in C, it's really easy to mix
up states-as-indices and their corresponding functions or transitions, so I pull
out X-macros to make it easier to eyeball that relationship.

The design of the protocol and the GetMsg() interface are intended to prevent
trivial buffer-overflow issues.  I pull the length of the message right off the
wire.  Of course, a malicious user could pad the message with additional bytes,
which will be at the top of the buffer the next time I process a transaction,
but the server will only interpret it in terms of the next state.

I don't know that this solution will withstand really intense fuzzing or
analysis, but I certainly tried to make it durable in that sense.


Finally, one might note that I do something strange with forcing the recv()
sockets into nonblocking mode.  When you're doing point-to-point IPC, this is
generally unnecessary, but in my experience different proxy solutions can buffer
TCP/IP sockets differently.  Notably, I've seen applications that do things like
msglen = recv(fd, buf, BIGLEN) break under proxy if the client has two
transactions queued and the server expects to recv() a single transaction at a
time.

There is much less to be said about the client, as the server leaves the client
code relatively trivial.  I had considered writing a C client and implementing
the Python client that way, but that would have completely (instead of just
mostly) obviated the client side of things
