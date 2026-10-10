"""Faction registry excludes class ordinal zero and unrelated definition classes."""
import struct
import unittest
from audit_factionregistry import string_array,registry,lookup,compare_registry


class FactionTests(unittest.TestCase):
    def test_plan_keeps_registry_candidates_separate(self):
        targets={0:('FACTION','NULL'),1:('FACTION','OTHER'),2:('FACTION','A'),
                 3:('OBJECT','A'),4:('FACTION','A')}
        row=compare_registry(targets,[0,2,3],['A'])[0]
        self.assertEqual(row['status'],'changed')
        self.assertEqual([c['slot'] for c in row['source_candidates_in_planned_slots']],[2])
        self.assertEqual([c['slot'] for c in row['planned_candidates']],[2,4])
        del targets[4]
        row=compare_registry(targets,[0,2,3],['A'])[0]
        self.assertEqual(row['status'],'unique')
        self.assertEqual(row['source_candidates_in_planned_slots'][0]['class_ordinal'],1)
        self.assertEqual(row['planned_candidates'][0]['class_ordinal'],2)
        with self.assertRaises(ValueError): compare_registry(targets,[0,0],['A'])
        with self.assertRaises(ValueError): compare_registry(targets,[9],['A'])

    def test_bounded_literal_strings(self):
        raw=struct.pack('<I',3)+b'FACTION_TEST\0\0OTHER\0'
        self.assertEqual(string_array(raw),['FACTION_TEST','','OTHER'])
        self.assertEqual(string_array(bytes(4)),[])
        for length in range(len(raw)):
            with self.assertRaises(ValueError): string_array(raw[:length])
        for bad in (raw+b'\0',struct.pack('<I',0xffffffff),struct.pack('<I',1)+b'\xff\0'):
            with self.assertRaises(ValueError): string_array(bad)

    def test_class_filter_null_ordinal_and_crc_candidates(self):
        rows=[{'definition':d,'name':n} for d,n in
              [('FACTION','NULLDEF_FACTION'),('OBJECT','FACTION_TEST'),('FACTION','FACTION_TEST'),
               ('FACTION',''),('FACTION','LOOKUP_ZERO_SY2CT3I'),('FACTION','FACTION_TEST')]]
        groups=registry(rows)
        self.assertEqual(lookup(groups,'NULLDEF_FACTION')['status'],'missing')
        self.assertEqual([r['slot'] for r in lookup(groups,'FACTION_TEST')['candidates']],[2,5])
        self.assertEqual(lookup(groups,'FACTION_TEST')['status'],'ambiguous')
        self.assertEqual([r['slot'] for r in lookup(groups,'')['candidates']],[3,4])
        self.assertEqual(lookup(groups,'')['status'],'ambiguous')
        self.assertEqual(lookup(groups,'MISSING')['status'],'missing')


if __name__=='__main__':
    unittest.main()
