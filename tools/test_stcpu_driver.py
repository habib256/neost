#!/usr/bin/env python3
"""Contrôles du pilote RTL : refuser les captures invalides avant tout verdict."""
import subprocess
import tempfile
from pathlib import Path
import unittest

from build_stcpu_bench import BINARY, BUILD
from make_stcpu_rom import P


@unittest.skipUnless(BINARY.exists(), 'Build Vstcpu_bench first')
class DriverChecks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.rom = self.root / 'probe.img'
        self.target = self.root / 'dump.bin'
        # Erreur d'adresse avec pile impaire : double faute, CPU arrêté.
        p = P()
        p.w(0x4FF9).l(0x1001)
        p.w(0x3038, 1)
        self.rom.write_bytes(p.done())

    def run_case(self, frames='1', addr='20000', size='4'):
        return subprocess.run([str(BINARY), str(self.rom), frames, addr, size, str(self.target)],
                              cwd=BUILD, capture_output=True, text=True, timeout=30)

    def test_bad_arguments(self):
        for args in [('0','20000','4'), ('-1','20000','4'), ('1x','20000','4'),
                     ('1','400000','4'), ('1','3fffff','2'), ('1','20000','-1'),
                     ('1','20000','0')]:
            with self.subTest(args=args):
                self.assertEqual(self.run_case(*args).returncode, 2)
                self.assertFalse(self.target.exists())

    def test_short_rom(self):
        self.rom.write_bytes(b'\0' * 8)
        r = self.run_case()
        self.assertEqual(r.returncode, 2)
        self.assertIn('Incomplete ROM', r.stderr)
        self.assertFalse(self.target.exists())

    def test_halted_cpu(self):
        r = self.run_case()
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertIn('halted=1', r.stderr)
        self.assertFalse(self.target.exists())


if __name__ == '__main__':
    unittest.main()
