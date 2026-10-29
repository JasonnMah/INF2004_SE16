#!/usr/bin/env python3
import unittest
from compare_uart_capture import crc16_x25, extract_capture
class CaptureTests(unittest.TestCase):
    def record(self,data=b"\x00\x7d\x7e\xff",identity=1):
        return (f"[BINARY BEGIN] completion={identity} length={len(data)} crc16_x25={crc16_x25(data):04x}\n"
                f"[BINARY] offset=0000 hex={data.hex()}\n[BINARY END] completion={identity}\n")
    def test_known_crc(self): self.assertEqual(crc16_x25(b"123456789"),0x906e)
    def test_binary(self): self.assertEqual(extract_capture(self.record()),(b"\x00\x7d\x7e\xff",1))
    def test_empty(self): self.assertEqual(extract_capture(self.record(b"")),(b"",1))
    def test_truncated_latest(self):
        with self.assertRaisesRegex(ValueError,"missing BINARY END"):
            extract_capture(self.record()+self.record(identity=2).split("[BINARY END]")[0])
    def test_crc(self):
        with self.assertRaisesRegex(ValueError,"CRC mismatch"):
            extract_capture(self.record().replace("007d7eff","017d7eff"))
    def test_offset(self):
        with self.assertRaisesRegex(ValueError,"out-of-order"):
            extract_capture(self.record().replace("offset=0000","offset=0001"))
    def test_identity(self):
        with self.assertRaisesRegex(ValueError,"ID mismatch"):
            extract_capture(self.record().replace("END] completion=1","END] completion=2"))
    def test_selected(self):
        self.assertEqual(extract_capture(self.record()+self.record(b"abc",2),1)[1],1)
if __name__=="__main__": unittest.main()
