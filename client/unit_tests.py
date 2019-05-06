import unittest
import threading
import client

def MCHelper(result, idx, name, message):
  try:
    conn = client.client(name, "A")
    conn.msg(message)
    conn.disconnect()
  except:
    pass
  else:
    result[idx] = True

class ServerTestCase(unittest.TestCase):
  def setUp(self):
    self.name = "MyTestCase"
    self.password = "A"
    self.conn = client.client(self.name, self.password, defer=True)
    self.passed = False

  def tearDown(self):
    self.conn.disconnect()

  def test_EstablishConection(self):
    try:
      self.conn.connect()
    except:
      pass
    else:
      self.passed = True
    self.assertTrue(self.conn._connected)
    self.assertTrue(self.passed)

  def test_SendMsg(self):
    try:
      self.conn.connect()
      self.conn.msg("Hello!  This is a test!")
    except:
      pass
    else:
      self.passed = True
    self.assertTrue(self.passed)

  def test_SendMsgDouble(self):
    try:
      self.conn.connect()
      self.conn.msg("Hello!  This is the first message!")
      self.conn.msg("Hello!  This is the second message.")
    except:
      pass
    else:
      self.passed = True
    self.assertTrue(self.passed)

  def test_PasswordFail(self):
    self.conn.password = "B"
    self.assertRaises(client.ClientError, self.conn.connect)

  def test_ConnectDisconnectConnect(self):
    try:
      self.conn.connect()
      self.conn.disconnect()
      self.conn.connect()
      self.conn.msg("Hello!")
    except:
      pass
    else:
      self.passed = True
    self.assertTrue(self.passed)

  def test_MultiClient(self):
    try:
      n    = 10
      res  = [False]*n
      name = ["MyTest_"+str(i) for i in range(0,n)]
      msg  = ["Hello"+("!"*i) for i in range(1,n+1)]
      tid  = [threading.Thread(target=MCHelper, args=(res, i, name[i], msg[i]))
              for i in range(0,n)]
      [t.start() for t in tid]
      [t.join() for t in tid]
    except:
      raise
    else:
      self.passed = True
    self.assertTrue(self.passed)
    [self.assertTrue(r) for r in res]


if __name__ == '__main__':
    unittest.main()
