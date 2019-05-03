import socket
import struct

class ClientError(Exception):
  pass

class Client(object):
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
      self.sfd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      self.sfd.connect((self.ip, self.port))
      self._send_name()
      self._send_pass()
      self._connected = True
    except:
      # We don't know where we are in the state machine, so we just disconnect.
      # The server should be able to handle this OK.
      self._connected = False
      self.sfd.close()
      raise

  def _send_name(self):
    self.sfd.send(b'\1')
    self.sfd.send(struct.pack("<H",len(self.name)))
    self.sfd.send(self.name.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise ClientError("Server rejected NAME transaction")

  def _send_pass(self):
    self.sfd.send(b'\2')
    self.sfd.send(struct.pack("<H",len(self.password)))
    self.sfd.send(self.password.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise ClientError("Server rejected PASS transaction")

  def send_msg(self,msg):
    """
    Sends a message, provided the Client is connected.  If we hit an error, then
    we're probably desynchronized from the server state machine, so we
    disconnect.  The user will have to reconnect.
    """
    if not self._connected:
      raise MsgError("Must be connected.")
    try:
      self.sfd.send(b'\3')
      self.sfd.send(struct.pack("<H",len(msg)))
      self.sfd.send(msg.encode())
      if struct.unpack("<B",self.sfd.recv(1))[0]:
        raise ClientError("Server rejected MSG transaction")
    except:
      self.sfd.disconnect()

  def disconnect(self):
    self._connected = False
    self.sfd.send(b'\4')
    self.sfd.recv(1)
    self.sfd.close()

  # Python will close the sockets on destruction, so no need to handle that.
  def __init__(self, name, password, host='localhost', port=5555):
    self.name = name
    self.password = password
    self.host = host
    self.ip = socket.gethostbyname(host)
    self.port = port
    self._connected = False
    self.connect(name, password)

