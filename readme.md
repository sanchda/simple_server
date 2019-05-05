### Coding Exercise (Implementation)

This program implements a simple C server and corresponding Python client for
serializing messages to a file on the server machine.  Builds are passing on:
 * Windows, MSVC (32 and 64-bit; tested on Win10)
 * Windows, MinGW (64-bit)
 * Linux,   GCC-8 (64-bit)


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


## Compatibility Notes

I went ahead and wrote this to be compatible with both Windows and Linux.  I
admit that although Windows is working, I did neglect to consider a few
differences in the socket API between the two--notably, whereas POSIX was
forced into signed-int sockets many years ago, Windows correctly withholds (~0)
as an error state and otherwise uses unsigned integers.  This is smoothed over
where it is really relevant.

Also, whereas POSIX grants file descriptors from the bottom-up, Windows does
not necessarily follow this tradition.  Accordingly, if I misunderstood the
behavior of FD_SETSIZE on Windows, then under load it may erroneously hangup
on connection requests (I understand the default limit to be 0-8192).  The
simple solution would be to add a level of indirection between the FDs and
the globals hanging onto connection state, which I did not do for simplicity.


## Editorial 

In this code I use conventions (e.g., `if(10>(n+(m=syscall()))) DoThing();`)
for the sake of readability/brevity which are not advisable when many hands are
working on a given project.  I have tried to structure the code in a way that
makes it easy to read as a single file, but it should be relatively easy to
see how it might be refactored into a proper project.  I intended this as a
convenience to the reviewers--hopefully it doesn't defeat that goal.

Overall, I hope the team will find the this approach to be a little beyond the
scope of the exercise, but thoughtful in its communication of the author's
experience with network code (albeit on Linux quite a bit more than Windows). I
think the state machine approach very elegantly constrains the behavior of the
server, even in substantially more sophisticated protocols.  Although I do
probably overuse the technique here, I also believe the use of X-macros defend
well against many easy-to-introduce state desynchronization bugs.  Socket-level
details are a bit fluffy--I'm not using fork() on Linux, and on Windows the
poll()/recv()/send() primitives are barely correct, so the whole EINTR handling
hints at a category of issues not completely relevant for this exercise.
