#!/usr/bin/python

import os
import sys
import unittest
import time
import subprocess
import re
import shutil

class CGCOS(unittest.TestCase):
    def setUp(self):
        self._cwd = os.getcwd()
        dirname = os.path.dirname(__file__)
        if dirname:
            os.chdir(dirname)
        self.run_cmd(['make', 'clean'])
        self.run_cmd(['make'])

    def tearDown(self):
        os.chdir(self._cwd)

    def run_cmd(self, cmd):
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.communicate()

    def run_read(self, flag, testfile):
        return self.run_cmd(['readcgcef', flag, "--wide",
                             os.path.join('files', testfile)])

    def check_results(self, results, expected):
        expected_re = re.compile(expected)
        for line in results:
            if expected_re.search(line):
                return

        # KLUDGE alert. Just throw an error
        self.assertEqual(expected, results)

    def test_good(self):
        results = self.run_read('-d', 'cgc-testcase')
        self.check_results(results, "There is no dynamic section in this file")

        results = self.run_read('-e', 'cgc-testcase')
        self.check_results(results, "LOAD\s+0x000000 0x08048000 0x08048000")

        results = self.run_read('-h', 'cgc-testcase')
        self.check_results(results, "Magic:\s+7f 43 47 43 01 01 01 43 01 4d 65 72 69 6e 6f 00")

        results = self.run_read('-l', 'cgc-testcase')
        self.check_results(results, "LOAD\s+0x000000 0x08048000 0x08048000")
        self.check_results(results, "Entry point 0x8048074")
        self.check_results(results,
                           "CGCEf file type is EXEC \(Executable file\)")

        results = self.run_read('-n', 'cgc-testcase')
        self.assertEqual(results[0], "")

        results = self.run_read('-r', 'cgc-testcase')
        self.assertEqual(results[0], "")

        results = self.run_read('-s', 'cgc-testcase')
        self.check_results(results, "FUNC\s+GLOBAL\s+[0-9]\s+main")
        self.check_results(results, "NOTYPE\s+GLOBAL\s+[0-9]\s+_start")
        self.check_results(results, "NOTYPE\s+GLOBAL\s+[0-9]\s+_end")

        results = self.run_read('-t', 'cgc-testcase')
        self.assertEqual(results[0], "")

        results = self.run_read('-D', 'cgc-testcase')
        self.assertEqual(results[0], "")

        results = self.run_read('-S', 'cgc-testcase')
        self.check_results(results, "[[\s*[0-9]+\]\s+\.text\s+PROGBITS\s+[0-9a-f]+")
        self.check_results(results, "[[\s*[0-9]+\]\s+\.shstrtab\s+STRTAB\s+[0-9a-f]+")
        self.check_results(results, "[[\s*[0-9]+\]\s+\.strtab\s+STRTAB\s+[0-9a-f]+")
        self.check_results(results, "[[\s*[0-9]+\]\s+\.symtab\s+SYMTAB\s+[0-9a-f]+")

if __name__ == '__main__':
    unittest.main()


# vim: tabstop=8 expandtab shiftwidth=4 softtabstop=4
