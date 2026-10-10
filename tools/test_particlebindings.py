"""Particle symbols use literal strings and the native public-name filter."""
import unittest
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from test_big_entry_markers import archive
from audit_particlebindings import particle_symbols,particle_fields,listed_particle_entries
from test_fmp_links import crc0


class ParticleTests(unittest.TestCase):
    def test_public_literal_names_and_duplicates(self):
        a,b='LOOKUP_IOAQYHT9HSJ4','LOOKUP_MC32E53Z7H9W'
        self.assertEqual(crc0(a),crc0(b))
        rows=[{'id':i+1,'type':0,'name':name} for i,name in enumerate((a,b,'Private[0]','Public','Public'))]
        groups=particle_symbols(rows)
        self.assertEqual(len(groups[a]),1)
        self.assertEqual(len(groups[b]),1)
        self.assertNotIn('Private[0]',groups)
        self.assertNotIn('public',groups)
        self.assertEqual([r['id'] for r in groups['Public']],[4,5])
        for name in ('','bad\0name','nonascii\u00e9'):
            with self.assertRaises(ValueError): particle_symbols([{'id':1,'type':0,'name':name}])

    def test_complete_bank_listing(self):
        bank={'bank':'PARTICLE_MAIN_PC','count':1,'entries':[]}
        with self.assertRaisesRegex(ValueError,'filtered or incomplete'):
            listed_particle_entries({'banks':[bank]})
        bank['entries']=[{'id':1,'type':0,'name':'SUNBEAMS'}]
        self.assertEqual(listed_particle_entries({'banks':[bank]}),bank['entries'])
        with self.assertRaises(ValueError): listed_particle_entries({'banks':[bank,bank]})
        with self.assertRaises(ValueError): listed_particle_entries({'banks':[]})

    def test_cli_coverage_and_report_preservation(self):
        tool=Path(__file__).resolve().parents[1]/'build/forge-tools.exe'
        with tempfile.TemporaryDirectory(prefix='forge-particle-') as directory:
            root=Path(directory)
            source=root/'source'
            levels=source/'data/Levels'
            levels.mkdir(parents=True)
            accepted,refused=levels/'accepted.tng',levels/'refused.tng'
            accepted.write_text('Version 2;\nXXXSectionStart NULL;\nNewThing Object;\n'
                'Player -1;\nUID 1;\nDefinitionType "PARTICLE_EMITTER_PLACEABLE";\nStartCTCDParticleEmitter;\n'
                'ParticleTypeName\t "SUNBEAMS"\t ;\nEndCTCDParticleEmitter;\nEndThing;\nXXXSectionEnd;\n',
                encoding='ascii')
            reviewed=subprocess.run([str(tool),'world','review-storage',str(accepted),'--json'],
                                    capture_output=True,text=True,timeout=30)
            self.assertEqual(reviewed.returncode,0,reviewed.stdout+reviewed.stderr)
            self.assertTrue(json.loads(reviewed.stdout)['storage_supported'])
            refused.write_text('ParticleTypeName "MISSING";\n',encoding='ascii')
            coverage=root/'coverage.json'
            artifact={'sources':[{'root':str(source),'input_sha256':{
                str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (accepted,refused)},
                'reviews':[{'path':str(levels),'refusals':[{'path':str(refused)}]}]}]}
            coverage.write_text(json.dumps(artifact),encoding='utf-8')
            effects,output=root/'effects.big',root/'report.json'
            archive(effects,b'BIGB',[('PARTICLE_MAIN_PC',111,[(1,0,'SUNBEAMS',b'opaque')])])
            before=effects.read_bytes()
            args=[sys.executable,str(Path(__file__).with_name('audit_particlebindings.py')),
                  '--coverage',str(coverage),'--effects',str(effects),'--tool',str(tool),'--out',str(output)]
            run=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            report=json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(report['public_symbols'],1)
            self.assertEqual([r['name'] for r in report['sources'][0]['references']],['SUNBEAMS'])
            self.assertEqual(report['findings'],0)
            previous=output.read_bytes()
            extra=levels/'new.tng'
            extra.write_text('',encoding='ascii')
            run=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(run.returncode,0)
            self.assertIn('file set changed',run.stderr)
            self.assertEqual(output.read_bytes(),previous)
            extra.unlink()
            accepted.write_text('changed',encoding='ascii')
            run=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(run.returncode,0)
            self.assertIn('stale thing-text coverage',run.stderr)
            self.assertEqual(output.read_bytes(),previous)
            self.assertEqual(effects.read_bytes(),before)

    def test_accepted_whitespace(self):
        for separator in (' ', '\t', ' \t '):
            for suffix in (';', ' ;', '\t ;'):
                with self.subTest(separator=separator,suffix=suffix):
                    self.assertEqual(particle_fields('ParticleTypeName'+separator+'"SUNBEAMS"'+suffix),
                                     [{'name':'SUNBEAMS','line':1}])
        self.assertEqual(particle_fields('ParticleTypeNameOther "MISSING";'),[])

    def test_accepted_field_locations(self):
        rows=particle_fields('Version 2;\n  ParticleTypeName "SUNBEAMS";\nParticleTypeName "";\n')
        self.assertEqual(rows,[{'name':'SUNBEAMS','line':2},{'name':'','line':3}])
        with self.assertRaises(ValueError): particle_fields('ParticleTypeName missing_quotes;')


if __name__=='__main__':
    unittest.main()
