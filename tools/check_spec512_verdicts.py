#!/usr/bin/env python3
"""Régression issue #44 : SKIP partiel, régressions présentes et références préservées.

Sans ROM, disquette ou exécutable ; teste le verdict du harnais, pas les pixels.
"""
import contextlib
import io
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import Mock, patch

import run_spec512_truth as spectrum


def main():
    expected = {'st': {'IMAGE.SPC': 0}, 'ste': {'IMAGE.SPC': 0}}
    with TemporaryDirectory() as tmp:
        ref = Path(tmp) / 'ref.json'
        original = json.dumps(expected)
        ref.write_text(original)
        cases = [
            ('all ROMs present', expected, [], False, 0),
            ('STE ROM absent', {'st': expected['st']}, ['ste'], False, 77),
            ('ST ROM absent', {'ste': expected['ste']}, ['st'], False, 77),
            ('all ROMs absent', {}, ['st','ste'], False, 77),
            ('present ST regresses', {'st': {'IMAGE.SPC': 1}}, ['ste'], False, 1),
            ('present image disappears', {'st': {}}, ['ste'], False, 1),
            ('unknown machine has no reference', {'new': {}}, ['ste'], False, 1),
            ('partial write preserves reference', {'st': expected['st']}, ['ste'], True, 77),
            ('empty write preserves reference', {}, ['st','ste'], True, 77),
        ]
        for name, verdict, skipped, write, want in cases:
            with patch.object(spectrum,'REF',ref), contextlib.redirect_stdout(io.StringIO()):
                got = spectrum.finish_results(verdict,skipped,write)
            if got != want or ref.read_text() != original:
                print(f'FAIL {name}: code {got}, expected {want}'); return 1
        with patch.object(spectrum,'REF',ref), contextlib.redirect_stdout(io.StringIO()):
            if spectrum.finish_results(expected,[],True) != 0 or json.loads(ref.read_text()) != expected:
                print('FAIL complete write'); return 1
        ref.unlink()
        with patch.object(spectrum,'REF',ref), contextlib.redirect_stdout(io.StringIO()):
            if spectrum.finish_results({'st':expected['st']},['ste']) != 1:
                print('FAIL missing reference accepted'); return 1
        # Le vrai main ne doit lancer que la machine dont la ROM existe.
        disk, binary = Path(tmp)/'disk.st', Path(tmp)/'headless'
        disk.write_bytes(b'disk'); binary.touch(); (Path(tmp)/'st.img').touch()
        ref.write_text(json.dumps({'st':{},'ste':expected['ste']}))
        fat = Mock(); fat.walk.return_value = []
        with patch.multiple(spectrum, ROOT=Path(tmp), DISK=disk, HEADLESS=binary, REF=ref,
                            RUNS=(('st','st.img'),('ste','ste.img'))), \
                patch.object(sys,'argv',['run_spec512_truth.py']), \
                patch.object(spectrum,'Fat',return_value=fat) as parse, \
                patch.object(spectrum.subprocess,'run') as run, contextlib.redirect_stdout(io.StringIO()):
            if spectrum.main() != 77 or run.call_count != 1 or run.call_args.args[0][1] != 'st.img':
                print('FAIL main partial run'); return 1
            (Path(tmp)/'st.img').unlink(); run.reset_mock(); parse.reset_mock()
            if spectrum.main() != 77 or run.called or parse.called:
                print('FAIL main without ROMs'); return 1
    print('SPECTRUM 512 VERDICT CHECKS OK (13 cases)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
