"""Music IDs remain local keys; names offsets, paths and framing are checked."""
import math
import struct
import unittest
from audit_musicbindings import music_entries, cache_music_sets
from test_fmp_links import crc0


class MusicTests(unittest.TestCase):
    def test_music_sets_use_array_positions_and_keep_all_candidates(self):
        raw = struct.pack('<I9i',3,0,0,123456,57,57,-1,15360,8,99)
        entries = [{'music_id':15360,'array_index':7}]
        result = cache_music_sets(raw,entries)
        self.assertFalse(result['has_all_cache_set_slots'])
        self.assertEqual(result['rows'][0]['cache_arguments'],[])
        row = result['rows'][2]
        self.assertEqual([a['status'] for a in row['cache_arguments']],['missing','unique'])
        self.assertEqual(row['words'][2],99)
        self.assertFalse(row['member_8_reviewed'])
        entries.append({'music_id':15360,'array_index':9})
        duplicate = cache_music_sets(raw,entries)['rows'][2]['cache_arguments'][1]
        self.assertEqual(duplicate['status'],'ambiguous')
        self.assertEqual(duplicate['registered_array_indices'],[7,9])
        for length in range(len(raw)):
            with self.assertRaises(ValueError): cache_music_sets(raw[:length],entries)
        with self.assertRaises(ValueError): cache_music_sets(raw+b'\0',entries)

    def test_local_id_and_filename_offset_are_distinct(self):
        pool = {15360:(crc0('Track.wma'),'Track.wma')}
        raw = struct.pack('<IIifB',1,15360,15360,0.5,1)
        row = music_entries(raw,pool)[0]
        self.assertEqual(row['music_id'],15360)
        self.assertEqual(row['relative_ogg'],'Track.ogg')
        self.assertEqual(music_entries(struct.pack('<IIifB',1,3,-1,0,0),{})[0]['filename'],'')
        self.assertEqual(music_entries(bytes(4),{}),[])

    def test_unsigned_storage_matches_signed_registration_keys(self):
        for stored, signed in ((0xffffffff,-1),(0x80000000,-0x80000000),(0x7fffffff,0x7fffffff)):
            entries = music_entries(struct.pack('<IIifB',1,stored,-1,1.0,0),{})
            sets = struct.pack('<I9i',3,0,0,0,0,0,0,signed,signed,0)
            arguments = cache_music_sets(sets,entries)['rows'][2]['cache_arguments']
            self.assertEqual([a['status'] for a in arguments],['unique','unique'])
            self.assertEqual(entries[0]['music_id'],stored)
            self.assertEqual(entries[0]['registered_key'],signed)

    def test_malformed_or_escaping_input(self):
        good = struct.pack('<IIifB',1,1,4,1.0,0)
        pool = {4:(crc0('Track.wma'),'Track.wma')}
        for length in range(len(good)):
            with self.assertRaises(ValueError): music_entries(good[:length],pool)
        for bad in (good+b'\0',struct.pack('<IIifB',1,1,-2,1.0,0),
                    struct.pack('<IIifB',1,1,4,math.nan,0),struct.pack('<IIifB',1,1,4,1.0,2)):
            with self.assertRaises(ValueError): music_entries(bad,pool)
        for name in ('../Track.wma','C:\\Track.wma','\\Track.wma','Track.txt','Bad\0.wma'):
            with self.assertRaises(ValueError): music_entries(good,{4:(crc0(name),name)})
        with self.assertRaises(ValueError): music_entries(good,{4:(0,'Track.wma')})


if __name__ == '__main__':
    unittest.main()
