#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wrapper/guard tests for run-fileops-safety.sh and tst_fileops_safety (no GUI, no real trash)."""
import os
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = ROOT / "tests/transfer/run-fileops-safety.sh"
BASE = ROOT / ".state/fileops-wrapper-tests"


def reset_signals():
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, signal.SIG_DFL)


class WrapperSignals(unittest.TestCase):
    def setUp(self):
        BASE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=BASE)
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)

    def run_signalled(self, sig):
        stub = self.work / "stub.sh"
        stub.write_text('#!/bin/sh\necho "$FILEOPS_SAFETY_HOME" > "%s/home"\ntouch "%s/ready"\n'
                        'exec sleep 120\n' % (self.work, self.work))
        stub.chmod(0o755)
        tmpdir = self.work / "tmp"
        tmpdir.mkdir()
        env = dict(os.environ, TMPDIR=str(tmpdir))
        proc = subprocess.Popen(["bash", str(WRAPPER), str(stub)], env=env, preexec_fn=reset_signals,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + 20
            while not (self.work / "ready").exists() and time.time() < deadline:
                time.sleep(0.05)
            self.assertTrue((self.work / "ready").exists(), "wrapper never started the stub")
            home = Path((self.work / "home").read_text().strip())
            self.assertTrue(home.is_dir())
            self.assertEqual(home.parent, tmpdir)
            proc.send_signal(sig)
            rc = proc.wait(timeout=30)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        return home, rc

    def check(self, sig):
        home, rc = self.run_signalled(sig)
        self.assertEqual(rc, 128 + int(sig))
        self.assertFalse(home.exists(), "temporary HOME left behind after %s" % sig.name)
        self.assertEqual(list((self.work / "tmp").iterdir()), [])

    def test_term_cleans_up(self):
        self.check(signal.SIGTERM)

    def test_int_cleans_up(self):
        self.check(signal.SIGINT)

    def test_hup_cleans_up(self):
        self.check(signal.SIGHUP)


class GuardRefusesForgedEnvironment(unittest.TestCase):
    """Running the binary by hand with forged FILEOPS_SAFETY_HOME/HOME and an inherited bus must refuse."""

    def setUp(self):
        self.binary = os.environ.get("FILEOPS_TEST_BINARY")
        if not self.binary:
            self.skipTest("FILEOPS_TEST_BINARY not set")
        BASE.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=BASE, prefix="fileops-safety-home-")
        self.addCleanup(self.tmp.cleanup)

    def forged(self, **extra):
        home = Path(self.tmp.name)
        (home / "data").mkdir(exist_ok=True)
        (home / "run").mkdir(exist_ok=True)
        env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / "data"), XDG_RUNTIME_DIR=str(home / "run"),
                   FILEOPS_SAFETY_HOME=str(home), QT_QPA_PLATFORM="offscreen",
                   DBUS_SESSION_BUS_ADDRESS="unix:path=%s/inherited-bus" % home)
        env.update(extra)
        # "-functions" only lists the tests: harmless if the guard were (wrongly) passed.
        return subprocess.run([self.binary, "-functions"], env=env, capture_output=True, text=True, timeout=60)

    def test_inherited_bus_without_marker_is_refused(self):
        res = self.forged()
        self.assertEqual(res.returncode, 2, res.stdout + res.stderr)
        self.assertIn("refusing to run", res.stderr)

    def test_forged_marker_not_started_by_dbus_run_session_is_refused(self):
        res = self.forged(FILEOPS_SAFETY_BUS="1")
        self.assertEqual(res.returncode, 2, res.stdout + res.stderr)

    def test_forged_marker_with_the_inherited_address_is_refused(self):
        addr = "unix:path=/nonexistent/bus"
        res = self.forged(FILEOPS_SAFETY_BUS="1", DBUS_SESSION_BUS_ADDRESS=addr, FILEOPS_SAFETY_INHERITED_BUS=addr)
        self.assertEqual(res.returncode, 2, res.stdout + res.stderr)

    def test_the_wrapper_gets_past_the_guard(self):
        res = subprocess.run(["bash", str(WRAPPER), self.binary, "-functions"], capture_output=True, text=True, timeout=60,
                             env=dict(os.environ, TMPDIR=self.tmp.name))
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("overwriteWriteFailureKeepsOriginal", res.stdout)


if __name__ == "__main__":
    unittest.main()
