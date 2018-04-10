#!/usr/bin/env python
import unittest
import inspect
import pysctrltp

class TestPacket(unittest.TestCase):
    def test_packet(self):
        pkt = pysctrltp.packet()
        self.assertEqual(pkt.len, 1)
        self.assertEqual(pkt.pid, int("0xDEAD", 16))
        pkt.len = 3
        pkt[0] = 0
        pkt[1] = 1
        pkt[2] = 2

if __name__ == "__main__":
    unittest.main()
