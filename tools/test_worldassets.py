"""Bounded minimap-table inventory tests."""
import struct
import unittest
from audit_worldassets import parse_minimap_table, text_symbol_candidates, review_text_query


class MinimapTableTests(unittest.TestCase):
    def test_text_crc_candidates_and_zero_id_fallback(self):
        entries = [{'name': name, 'id': identity, 'type': kind} for name, identity, kind in (
            ('LOOKUP_IOAQYHT9HSJ4', 1, 0), ('LOOKUP_MC32E53Z7H9W', 2, 1),
            ('LOOKUP_ZERO_SY2CT3I', 3, 0), ('ZERO_ID', 0, 99), ('UNSUPPORTED', 4, 2))]
        groups = text_symbol_candidates(entries)
        self.assertEqual(review_text_query('LOOKUP_IOAQYHT9HSJ4', groups), ('ambiguous', entries[:2]))
        self.assertEqual(review_text_query('LOOKUP_ZERO_SY2CT3I', groups), ('entry', [entries[2]]))
        self.assertEqual(review_text_query('ZERO_ID', groups), ('literal_fallback', [entries[3]]))
        self.assertEqual(review_text_query('UNSUPPORTED', groups), ('unsupported_text_type', [entries[4]]))
        self.assertEqual(review_text_query('missing', groups), ('literal_fallback', []))
        self.assertEqual(review_text_query('', groups), ('empty', []))
        for name in ('', 'BAD\0SYMBOL', '\xff'):
            with self.assertRaises(ValueError):
                text_symbol_candidates([{'name': name, 'id': 1, 'type': 0}])

    def test_names_and_signed_values_preserved(self):
        data = struct.pack("<I", 2) + b"MINIMAP_A\0" + struct.pack("<i", 6000)
        data += b"MINIMAP_B\0" + struct.pack("<i", -1)
        self.assertEqual(parse_minimap_table(data), {"MINIMAP_A": 6000, "MINIMAP_B": -1})
        self.assertEqual(parse_minimap_table(bytes(4)), {})
        for end in range(len(data)):
            with self.assertRaises(ValueError):
                parse_minimap_table(data[:end])

    def test_duplicate_unknown_and_unbounded_storage_refused(self):
        for data in [struct.pack("<I", 0xFFFFFFFF), bytes(4) + b"extra",
                     struct.pack("<I", 1) + b"\0" + bytes(4),
                     struct.pack("<I", 1) + b"\xff\0" + bytes(4),
                     struct.pack("<I", 2) + (b"same\0" + bytes(4)) * 2]:
            with self.assertRaises(ValueError):
                parse_minimap_table(data)


if __name__ == "__main__":
    unittest.main()
