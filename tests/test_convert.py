"""Run with python3 -m unittest discover -s tests -p 'test_convert.py'.

Use deterministic macOS-style file/md5 output and the real iconv executable.
All conversion takes place in a temporary copy of the translation directory.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ConvertTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copyfile(Path(__file__).resolve().parents[1] / 'Translations-macOS' / 'convert', self.root / 'convert')
        self.language = self.root / 'en.lproj'
        self.language.mkdir()
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        # BSD file uses -I; GNU file uses -i. Classify these known fixtures
        # deterministically so the same failure is exercised on either host.
        self.tool('file', '#!/bin/sh\necho "$2: text/plain; charset=utf-16le"\n')
        self.tool('md5', '#!/bin/sh\nprintf "MD5 (%s) = " "$1"\n'
                  + 'python3 -c \'import hashlib,sys; print(hashlib.md5(open(sys.argv[1], "rb").read()).hexdigest())\' "$1"\n')
        self.env = dict(os.environ, PATH=str(self.tools) + os.pathsep + os.environ['PATH'])

    def tool(self, name, contents):
        file = self.tools / name
        file.write_text(contents)
        file.chmod(0o755)

    def convert(self):
        return subprocess.run(['bash', str(self.root / 'convert')], env=self.env,
                              capture_output=True, text=True)

    def test_failed_conversion_preserves_original_and_can_retry(self):
        file = self.language / 'Localizable.strings'
        original = '"key" = "value";'.encode('utf-16le') + b'\xff'
        file.write_bytes(original)
        for _ in range(2):
            result = self.convert()
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(file.read_bytes(), original)
            self.assertFalse((self.language / '.Localizable.strings.conversion.md5').exists())
            self.assertFalse((self.language / 'Localizable.strings.tmp').exists())

    def test_successful_conversion_is_cached_after_conversion(self):
        file = self.language / 'Localizable.strings'
        expected = '"key" = "value";'.encode('utf-8')
        file.write_bytes(expected.decode().encode('utf-16le'))
        result = self.convert()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(file.read_bytes(), expected)
        cache = self.language / '.Localizable.strings.conversion.md5'
        self.assertEqual(cache.read_text().strip(), hashlib.md5(expected).hexdigest())
        self.tool('iconv', '#!/bin/sh\nexit 99\n')
        result = self.convert()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(file.read_bytes(), expected)


if __name__ == '__main__':
    unittest.main()
