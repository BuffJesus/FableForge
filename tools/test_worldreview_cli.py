"""Exercise the public read-only world storage command and its exit contract."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path(__file__).resolve().parents[1] / "build/forge-tools.exe")
    args = parser.parse_args()
    exe = args.exe.resolve()
    checks = 0
    with tempfile.TemporaryDirectory(prefix="forge-world-review-") as directory:
        root = Path(directory)

        def run(arguments, expected):
            nonlocal checks
            result = subprocess.run([str(exe), "world", "review-storage", *map(str, arguments)],
                                    capture_output=True, text=True, encoding="utf-8", check=False)
            assert result.returncode == expected, (arguments, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        tga = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 2, 1, 24, 0)
        tga += bytes([0, 60, 0, 255, 255, 255]) + bytes(8) + b'TRUEVISION-XFILE.\0'
        # A valid Ogg envelope with an arbitrary packet deliberately says
        # nothing about Vorbis decoding; the CLI must retain that scope.
        ogg = bytearray(b'OggS' + bytes([0, 6]) + bytes(8) + struct.pack('<III',123,0,0) + bytes([1,3]) + b'abc')
        crc = 0
        for value in ogg:
            crc ^= value << 24
            for _ in range(8):
                crc = ((crc << 1) ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
        struct.pack_into('<I',ogg,22,crc)
        switch = (b'Version 2;\nXXXSectionStart NULL;\nNewThing Switch;\nPlayer -1;\n'
                  b'UID 1;\nDefinitionType "SWITCH_TEST";\nEnvironmentDef THEME_TEST;\n'
                  b'TriggeredByThing SCRIPT_NAME;\nEndThing;\nXXXSectionEnd;\n')
        hook=(b'Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\n'
              b'UID 1;\nDefinitionType "OBJECT_TEST";\nStartCTCActionUseScriptedHook;\n'
              b'VersionNumber 1;\nReplacementObject 0;\nSoundName "";\nAnimationName "";\n'
              b'EndCTCActionUseScriptedHook;\nEndThing;\nXXXSectionEnd;\n')
        base_thing=(b'Version 2;\nXXXSectionStart NULL;\nNewThing Thing;\nPlayer 4;\n'
                    b'UID 1;\nDefinitionType "OBJECT_TEST";\nHealth 1.00000;\nObjectScale 1.00000;\n'
                    b'EndThing;\nXXXSectionEnd;\n')
        light=base_thing.replace(b'EndThing;',b'StartCTCLight;\nOverridden TRUE;\nInterted FALSE;\nInverted TRUE;\nEndCTCLight;\nEndThing;')
        brain=switch.replace(b'NewThing Switch',b'NewThing AICreature').replace(
            b'EnvironmentDef THEME_TEST;\nTriggeredByThing SCRIPT_NAME;',b'OverridingBrainName BRAIN_TEST;')
        scratchpad=brain.replace(b'SWITCH_TEST',b'CREATURE_TEST').replace(
            b'EndThing;',b'StartCTCAIScratchpad;\nEndCTCAIScratchpad;\nEndThing;')
        door=base_thing.replace(b'EndThing;',b'StartCTCDoor;\nOpen TRUE;\nDoorTriggerType 1;\nEndCTCDoor;\nEndThing;')
        fixtures = {
            "section-day.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart NULL%DayOnly;'),0),
            "section-night.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart Q_TEST%NightOnly;'),0),
            "section-numeric.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart 15360;'),0),
            "section-numeric-day.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart 15360%DayOnly;'),0),
            "section-day.gtg": (b'NEWMAP 7\n'+base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart NULL%DayOnly;')+b'ENDMAP\n',0),
            "section-case.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart NULL%dayonly;'),1),
            "section-unknown.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart NULL%Unknown;'),1),
            "section-empty-base.tng": (base_thing.replace(b'XXXSectionStart NULL;',b'XXXSectionStart %DayOnly;'),1),
            "script-name-number.tng": (base_thing.replace(b'EndThing;',b'ScriptName 15360;\nEndThing;'),1),
            "script-name-digit-first.tng": (base_thing.replace(b'EndThing;',b'ScriptName 1ROLE;\nEndThing;'),1),
            "script-name-quoted.tng": (base_thing.replace(b'EndThing;',b'ScriptName "ROLE";\nEndThing;'),1),
            "script-name-underscore.tng": (base_thing.replace(b'EndThing;',b'ScriptName _ROLE_2;\nEndThing;'),0),
            "script-name-null.tng": (base_thing.replace(b'EndThing;',b'ScriptName NULL;\nEndThing;'),0),
            "door-legacy.tng": (door,0),
            "door-legacy-noncanonical.tng": (door.replace(b'DoorTriggerType 1;',b'DoorTriggerType 2;'),1),
            "door-legacy-malformed.tng": (door.replace(b'DoorTriggerType 1;',b'DoorTriggerType 01;'),1),
            "door-legacy-duplicate.tng": (door.replace(b'EndCTCDoor;',b'DoorTriggerType 1;\nEndCTCDoor;'),1),
            "door-legacy-misplaced.tng": (base_thing.replace(b'EndThing;',b'DoorTriggerType 1;\nEndThing;'),1),
            "ai-scratchpad.tng": (scratchpad,0),
            "ai-scratchpad.gtg": (b'NEWMAP 7\n'+scratchpad+b'ENDMAP\n',0),
            "ai-scratchpad-categories.tng": (scratchpad.replace(b'EndCTCAIScratchpad;',b'ActiveGossipCategories 0;\nEndCTCAIScratchpad;'),1),
            "ai-scratchpad-player.tng": (scratchpad.replace(b'EndCTCAIScratchpad;',b'IsGossipForPlayer 0;\nEndCTCAIScratchpad;'),1),
            "ai-scratchpad-gossip.tng": (scratchpad.replace(b'EndCTCAIScratchpad;',b'Gossip[0] 15360;\nEndCTCAIScratchpad;'),1),
            "switch-environment-number.tng": (switch.replace(b'THEME_TEST',b'15360'),1),
            "switch-trigger-number.tng": (switch.replace(b'SCRIPT_NAME',b'15360'),1),
            "switch-environment-prefix.tng": (switch.replace(b'THEME_TEST',b'1THEME'),1),
            "switch-trigger-prefix.tng": (switch.replace(b'SCRIPT_NAME',b'1NAME'),1),
            "switch-underscore.tng": (switch.replace(b'THEME_TEST',b'_THEME_TEST').replace(b'SCRIPT_NAME',b'_SCRIPT_NAME'),0),
            "brain-number.tng": (brain.replace(b'BRAIN_TEST',b'15360'),1),
            "brain-number-prefix.tng": (brain.replace(b'BRAIN_TEST',b'1BRAIN'),1),
            "brain-quoted.tng": (brain.replace(b'BRAIN_TEST',b'"BRAIN_TEST"'),1),
            "brain-quoted-null.tng": (brain.replace(b'BRAIN_TEST',b'"NULL"'),1),
            "brain-underscore.tng": (brain.replace(b'BRAIN_TEST',b'_BRAIN_TEST'),0),
            "light-legacy.tng": (light,0),
            "light-legacy-nondefault.tng": (light.replace(b'Interted FALSE',b'Interted TRUE'),1),
            "light-legacy-malformed.tng": (light.replace(b'Interted FALSE',b'Interted 0'),1),
            "light-legacy-duplicate.tng": (light.replace(b'Interted FALSE;',b'Interted FALSE;\nInterted FALSE;'),1),
            "light-legacy-misplaced.tng": (base_thing.replace(b'EndThing;',b'Interted FALSE;\nEndThing;'),1),
            "base-thing-defaults.tng": (base_thing,0),
            "base-thing-health.tng": (base_thing.replace(b'Health 1.00000',b'Health 2'),1),
            "base-thing-scale.tng": (base_thing.replace(b'ObjectScale 1.00000',b'ObjectScale 2'),1),
            "hook.tng": (hook,0),
            "hook-nondefault.tng": (hook.replace(b"ReplacementObject 0",b"ReplacementObject 2856"),1),
            "hook-unversioned.tng": (hook.replace(b"VersionNumber 1;\n",b""),1),
            "brain.tng": (brain, 0),
            "switch.tng": (switch, 0),
            "switch.gtg": (b'NEWMAP 7\n' + switch + b'ENDMAP\n', 0),
            "switch-null.tng": (switch.replace(b'THEME_TEST', b'NULL'), 0),
            "packet.ogg": (ogg, 0),
            "damaged.ogg": (ogg[:-1] + b'x', 1),
            "trailing.ogg": (ogg + b'\0', 1),
            "lighting.tga": (tga, 0),
            "header-only.tga": (tga[:18], 1),
            "trailing.tga": (tga + b'\0', 1),
            "valid.tng": (b"Version 2;\n", 0),
            "refused.tng": (b"Version 2;\nUnknown 15360;\n", 1),
            "broken.wad": (b"not a WAD", 1),
            "unknown.bin": (b"Version 2;\n", 1),
            "empty.gtg": (b"", 0),
            "unicode-\u00e9.TNG": (b"Version 2;\n", 0),
            "object.tng": (b"Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\n"
                           b"UID 1;\nDefinitionType \"OBJECT_TEST\";\nEndThing;\nXXXSectionEnd;\n", 0),
            "region.wld": (b"START_INITIAL_QUESTS;\nQ_TEST;\nPersonalScriptMain;\nEND_INITIAL_QUESTS;\nMapUIDCount 0;\n"
                           b"ThingManagerUIDCount 0;\nNewRegion 1;\nRegionName \"Area\";\n"
                           b"NewDisplayName \"TXT_AREA\";\nRegionDef \"REGION_AREA\";\n"
                           b"MiniMapGraphic MAP_AREA;\nEndRegion;\n", 0),
        }
        for label,value,code in (('number',b'15360',1),('prefix',b'1MAP',1),
                                 ('quoted',b'"MAP_AREA"',1),('underscore',b'_MAP_AREA',0)):
            fixtures['minimap-'+label+'.wld']=(fixtures['region.wld'][0].replace(b'MAP_AREA',value),code)
        for label,value,code in (('number',b'15360',1),('prefix',b'1QUEST',1),
                                 ('quoted',b'"Q_TEST"',1),('underscore',b'_Q_TEST',0)):
            fixtures['initial-quest-'+label+'.wld']=(fixtures['region.wld'][0].replace(b'Q_TEST',value),code)
        for label,value,code in (('maximum',b'18446744073709551615',0),
                                 ('overflow',b'18446744073709551616',1),
                                 ('source-overflow',b'1844674165887537688224486',1),
                                 ('malformed',b'1junk',1)):
            fixtures['uid-'+label+'.tng']=(fixtures['object.tng'][0].replace(b'UID 1;',b'UID '+value+b';'),code)
        for name, (data, code) in fixtures.items():
            path = root / name
            path.write_bytes(data)
            before = hashlib.sha256(data).digest()
            report = json.loads(run([path, "--json"], code).stdout)
            assert report["storage_supported"] == (code == 0)
            assert report["source_admitted"] is False
            assert report["supported_units"] + report["refused_units"] == len(report["units"]) == 1
            if code:
                assert report["units"][0]["reason"]
            if name in ('uid-overflow.tng','uid-source-overflow.tng'):
                assert report['units'][0]['directive']=='UID'
                assert report['units'][0]['line']==5
                assert 'numeric value outside supported range' in report['units'][0]['reason']
            if name=='uid-malformed.tng':
                assert 'invalid numeric value' in report['units'][0]['reason']
            if name.startswith('script-name-') and code:
                assert report['units'][0]['directive']=='ScriptName'
                assert 'script name must be an unquoted identifier' in report['units'][0]['reason']
            if name.startswith('initial-quest-') and code:
                assert report['units'][0]['line']==2
                assert 'initial quest name must be an unquoted identifier' in report['units'][0]['reason']
            if name=='initial-quest-underscore.wld':
                assert report['units'][0]['unresolved_initial_quests']==['_Q_TEST','PersonalScriptMain']
            if name=='base-thing-defaults.tng':
                references=report['units'][0]['unresolved_thing_definitions']
                assert len(references)==1 and references[0]['thing_type']=='Thing'
                assert references[0]['definition']=='OBJECT_TEST' and references[0]['line']==6
            if name=='light-legacy.tng':
                references=report['units'][0]['unresolved_thing_definitions']
                assert len(references)==1 and references[0]['definition']=='OBJECT_TEST'
            if name=='door-legacy.tng':
                references=report['units'][0]['unresolved_thing_definitions']
                assert len(references)==1 and references[0]['definition']=='OBJECT_TEST'
            if name in ('door-legacy-noncanonical.tng','door-legacy-malformed.tng'):
                assert report['units'][0]['directive']=='CTCDoor.DoorTriggerType'
                assert 'unreviewed legacy door annotation' in report['units'][0]['reason']
            if name in ('light-legacy-nondefault.tng','light-legacy-malformed.tng'):
                assert report['units'][0]['directive']=='CTCLight.Interted'
                assert 'nondefault legacy light annotation' in report['units'][0]['reason']
            if name in ('base-thing-health.tng','base-thing-scale.tng'):
                assert 'nondefault legacy base Thing scalar' in report['units'][0]['reason']
                assert report['units'][0]['directive']==('Health' if name=='base-thing-health.tng' else 'ObjectScale')
            if name.startswith('brain-') and code:
                assert report['units'][0]['directive']=='OverridingBrainName'
                assert 'brain name must be an unquoted identifier' in report['units'][0]['reason']
            if name in ('brain.tng','brain-underscore.tng'):
                references = report['units'][0]['unresolved_thing_definitions']
                assert len(references)==2
                assert references[1]=={'thing_type':'AICreature','definition':'_BRAIN_TEST' if name=='brain-underscore.tng' else 'BRAIN_TEST',
                                       'line':7,'field':'OverridingBrainName','conditional':True}
            if name in ('ai-scratchpad.tng','ai-scratchpad.gtg'):
                references=report['units'][0]['unresolved_thing_definitions']
                assert [ref['definition'] for ref in references]==['CREATURE_TEST','BRAIN_TEST']
                if name.endswith('.gtg'): assert all(ref['world_map_slot']==7 for ref in references)
            if name.startswith('ai-scratchpad-'):
                unit=report['units'][0]
                assert unit['directive'].startswith('CTCAIScratchpad.') and unit['line']==9
                assert 'unsupported component scalar' in unit['reason']
            if name in ('switch-environment-number.tng','switch-environment-prefix.tng','switch-trigger-number.tng','switch-trigger-prefix.tng'):
                assert 'switch name must be an unquoted identifier' in report['units'][0]['reason']
                assert report['units'][0]['directive']==('EnvironmentDef' if 'environment' in name else 'TriggeredByThing')
            if name in ('switch.tng','switch.gtg','switch-null.tng','switch-underscore.tng'):
                references = report['units'][0]['unresolved_thing_definitions']
                assert len(references)==2
                environment = references[1]
                assert environment['field']=='EnvironmentDef' and environment['conditional'] is True
                assert environment['definition']==('' if name=='switch-null.tng' else '_THEME_TEST' if name=='switch-underscore.tng' else 'THEME_TEST')
                assert environment['line']==(8 if name=='switch.gtg' else 7)
                if name=='switch.gtg': assert environment['world_map_slot']==7
            if name == "packet.ogg":
                assert report['units'][0]['codec_checked'] is False
                assert report['units'][0]['metrics'] == {'pages':1,'packets':1,'payload_bytes':3,'stream_serial':123}
                assert 'envelope only' in report['units'][0]['scope']
            if name == "lighting.tga":
                assert report["units"][0]["metrics"] == {"width": 2, "height": 1, "bits_per_pixel": 24}
                assert 'bindings require resolution' in report["units"][0]["scope"]
            if name == "refused.tng":
                assert report["units"][0]["line"] == 2
                assert report["units"][0]["directive"] == "Unknown"
            if name == "region.wld":
                assert report["units"][0]["unresolved_initial_quests"] == ["Q_TEST", "PersonalScriptMain"]
                assert 'not game.bin' in report["units"][0]["initial_quest_scope"]
                region = report["units"][0]["unresolved_region_symbols"][0]
                assert region == {"region_slot": 1, "region_name": "Area", "display_name": "TXT_AREA",
                                  "region_definition": "REGION_AREA", "minimap_graphic": "MAP_AREA"}
            if name.startswith('minimap-'):
                if code:
                    assert report['units'][0]['directive']=='MiniMapGraphic'
                    assert 'minimap graphic name must be an unquoted identifier' in report['units'][0]['reason']
                    assert not report['units'][0].get('unresolved_region_symbols')
                else:
                    assert report['units'][0]['unresolved_region_symbols'][0]['minimap_graphic']=='_MAP_AREA'
            if name == "object.tng":
                assert report["units"][0]["unresolved_thing_definitions"] == [
                    {"thing_type": "Object", "definition": "OBJECT_TEST", "line": 6}]
            assert hashlib.sha256(path.read_bytes()).digest() == before
        for path in [root, root / "missing.tng"]:
            report = json.loads(run([path, "--json"], 1).stdout)
            assert not report["storage_supported"]
        for arguments in [[], ["--json"], [root / "valid.tng", "--unknown"],
                          [root / "valid.tng", "--json", "extra"]]:
            assert "usage:" in run(arguments, 2).stderr
        assert "Storage review: 1 supported, 0 refused" in run([root / "valid.tng"], 0).stdout
        assert "REFUSED" in run([root / "refused.tng"], 1).stdout
        assert "codec packets, granule semantics, playback" in run([root / "packet.ogg"], 0).stdout
        # A compact sparse-ID archive proves aggregation continues after a
        # refused entry, and that envelope acceptance is not payload acceptance.
        def wad(rows):
            def words(*values):
                return struct.pack("<" + "I" * len(values), *values)

            def name(value):
                data = value.encode("ascii")
                return words(len(data)) + data

            body = bytearray(32)
            table = bytearray(words(1, 0, len(rows)))
            for identity, key, payload in rows:
                body.extend(bytes((-len(body)) % 2048))
                offset = len(body)
                body.extend(payload)
                table.extend(words(42, identity, 0, len(payload), offset, 0))
                table.extend(name(key) + words(0, 1) + name("") + words(88) + bytes(88))
            body.extend(bytes((-len(body)) % 2048))
            body[:32] = words(0x42424242, 115, 2, 1, 2048, max(r[0] for r in rows), len(rows), len(body))
            return bytes(body + table)

        archive = root / "mixed.wad"
        original = wad([(7, "bad.tng", b"Unknown 1;\n"),
                        (31, "good.tng", b"Version 2;\n"),
                        (40, "unknown.bin", b"opaque")])
        archive.write_bytes(original)
        report = json.loads(run([archive, "--json"], 1).stdout)
        assert report["supported_units"] == report["refused_units"] == 2
        assert [u["entry_id"] for u in report["units"][1:]] == [7, 31, 40]
        assert report["units"][2]["supported"]
        assert archive.read_bytes() == original
        archive.write_bytes(wad([(31, "good.tng", b"Version 2;\n")]))
        report = json.loads(run([archive, "--json"], 0).stdout)
        assert report["supported_units"] == 2 and not report["source_admitted"]

        # A valid envelope must not hide a refused entry or stop before later ones.
        def big(marker=42):
            words = lambda *v: struct.pack('<'+'I'*len(v), *v)
            table = words(2, 2, 1, 99, 1)
            for identity, kind, name, info in [(1, 99, b'opaque', b''), (2, 2, b'narrators', words(0))]:
                table += words(marker, identity, kind, 0, 16, 0, len(name)) + name
                table += words(0, 0, len(info)) + info
            footer = words(1) + b'TEXT_ENGLISH_MAIN\0' + words(106, 2, 16, len(table), 1)
            return b'BIGB' + words(100, 16+len(table), len(footer)) + table + footer

        archive = root / 'mixed.big'
        original = big()
        archive.write_bytes(original)
        report = json.loads(run([archive, '--json'], 1).stdout)
        assert report['supported_units'] == 2 and report['refused_units'] == 1
        assert [u['entry_id'] for u in report['units'][1:]] == [1, 2]
        assert report['units'][2]['supported'] and archive.read_bytes() == original
        archive.write_bytes(big(0))
        report = json.loads(run([archive, '--json'], 1).stdout)
        assert len(report['units']) == 1 and 'entry marker 0' in report['units'][0]['reason']
        # Particle container framing is independently accepted; opaque particle
        # payloads must still refuse complete storage/source acceptance.
        from test_big_entry_markers import archive as make_archive
        particle=root/'effects.big'
        make_archive(particle,b'BIGB',[('PARTICLE_MAIN_PC',111,[(1,0,'SUNBEAMS',b'opaque')])])
        data=bytearray(particle.read_bytes())
        # The generic fixture intentionally uses legacy zero markers.
        directory=struct.unpack_from('<I',data,8)[0]
        fields=directory+4+len('PARTICLE_MAIN_PC')+1
        table=struct.unpack_from('<I',data,fields+8)[0]
        struct.pack_into('<I',data,table+12,42)
        particle.write_bytes(data)
        report=json.loads(run([particle,'--json'],1).stdout)
        assert report['supported_units']==1 and report['refused_units']==1
        assert report['units'][0]['scope']=='archive envelope'
        assert not report['storage_supported'] and not report['source_admitted']
        assert report['units'][1]['bank']=='PARTICLE_MAIN_PC'
        assert particle.read_bytes()==data
    print(f"World review CLI: {checks} checks passed; fixture bytes unchanged.")


if __name__ == "__main__":
    main()
