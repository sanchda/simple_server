import socket
import struct

class ClientError(Exception):
  pass

class client(object):
  """
  Implements the client side of the server protocol.  NAME and PASS type
  transactions are handled by the client at time of initialization, allowing
  the user to send messages or close the connection as desired.
  """
  def connect(self, name=None, password=None):
    if self._connected:
      raise
    self.name = self.name if name is None else name
    self.password = self.password if password is None else password
    try:
      self._getsock()
      self._send_name()
      self._send_pass()
      self._connected = True
    except:
      # We don't know where we are in the state machine, so we just disconnect.
      # The server should be able to handle this OK.
      self._connected = False
      self.sfd.close()
      raise

  def _getsock(self):
    if self._connected:
      raise ClientError("Already connected.")
      return
    self.sfd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self.sfd.connect((self.ip, self.port))

  def _send_name(self):
    self.sfd.send(bytearray(b'\1') +
                  struct.pack("<H", len(self.name)) +
                  self.name.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise ClientError("Server rejected NAME transaction")

  def _send_pass(self):
    self.sfd.send(bytearray(b'\2') +
                  struct.pack("<H", len(self.password)) +
                  self.password.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise ClientError("Server rejected PASS transaction")

  def msg(self,msg):
    """
    Sends a message, provided the Client is connected.  If we hit an error, then
    we're probably desynchronized from the server state machine, so we
    disconnect.  The user will have to reconnect.
    """
    if not self._connected:
      raise MsgError("Must be connected.")
      return
    try:
      self.sfd.send(bytearray(b'\3') +
                    struct.pack("<H", len(msg)) +
                    msg.encode())
      if struct.unpack("<B",self.sfd.recv(1))[0]:
        raise ClientError("Server rejected MSG transaction")
    except:
      self.sfd.close()

  def disconnect(self):
    self._connected = False
    try:
      self.sfd.send(b'\4')
      self.sfd.recv(1)
      self.sfd.close()
    except:
      pass

  def __init__(self, name, password, host='localhost', port=5555, defer=False):
    self.name = name
    self.password = password
    self.host = host
    self.defer = defer
    self.ip = socket.gethostbyname(host)
    self.port = port
    self._connected = False
    if not self.defer:
      self.connect(name, password)

