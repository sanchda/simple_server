import socket
import struct

class NameError(Exception):
  pass

class PassError(Exception):
  pass

class MsgError(Exception):
  pass


class Client(object):
  # Implementation of the Server protocol.  Naming stuff is hard.
  def connect(self, name, password):
    self.name = name
    self.password = password
    self.sfd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self.sfd.connect((self.ip, self.port))
    self._send_name()
    self._send_pass()

  def _send_name(self):
    self.sfd.send(b'\1')
    self.sfd.send(struct.pack("<H",len(self.name)))
    self.sfd.send(self.name.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise NameError()

  def _send_pass(self):
    self.sfd.send(b'\2')
    self.sfd.send(struct.pack("<H",len(self.password)))
    self.sfd.send(self.password.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise PassError()

  def send_msg(self,msg):
    self.sfd.send(b'\3')
    self.sfd.send(struct.pack("<H",len(msg)))
    self.sfd.send(msg.encode())
    if struct.unpack("<B",self.sfd.recv(1))[0]:
      raise MsgError()

  def close(self):
    self.sfd.send(b'\4')
    self.sfd.recv(1)      # doesn't matter, hung up.

  def __init__(self, name, password, host='localhost', port=5555):
    self.name = name
    self.password = password
    self.host = host
    self.ip = socket.gethostbyname(host)
    self.port = port
    self.connect(name, password)



