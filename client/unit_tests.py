import unittest
import client

class ServerTestCase(unittest.TestCase):
  def setUp(self):
    self.conn = client.client(self.name, self.password, defer=True)

  def test_EstablishConection(self):
    
