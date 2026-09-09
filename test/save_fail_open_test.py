"""Linux LE64/flock regression tests; run after make -C src all.

Uses only Python's standard library and the system C compiler. Fault-injection
helpers are built in a temporary directory, never installed with the library.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(sys.platform == "linux" and sys.byteorder == "little"
                     and struct.calcsize("P") == 8, "requires Linux LE64")
class SaveFailOpenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="save-test-build-")
        cls.probe = str(Path(cls.build.name) / "probe")
        cls.fault = str(Path(cls.build.name) / "flock.so")
        for source, output, extra in (
            ("save_fail_open_probe.c", cls.probe, []),
            ("save_fail_open_flock.c", cls.fault, ["-shared", "-fPIC"]),
        ):
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", *extra,
                            str(ROOT / "test" / source), "-o", output], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="save-test-")
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name) / "time.bin"
        suffix = str(uuid.uuid4().int % 10**18)
        self.shared = Path("/dev/shm") / ("faketime_shm_" + suffix)
        lock = Path("/dev/shm") / ("faketime_lock_" + suffix)
        for path in (self.shared, lock):
            # Register before creating, so a failed setup still cleans up.
            self.addCleanup(lambda p=path: p.unlink(missing_ok=True))
        self.shared.write_bytes(struct.pack("<IHHQQ" + "qq" * 4 + "Q",
                                           0x46544C42, 2, 96, 0, 0,
                                           0, -1, 0, -1, 0, -1, 0, -1, 0))
        lock.touch()
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith("FAKETIME") and k != "LD_PRELOAD"}
        self.env.update(LD_PRELOAD=str(ROOT / "src/libfaketime.so.1"),
                        FAKETIME="+0", FAKETIME_DONT_FAKE_MONOTONIC="1",
                        NO_FAKE_STAT="1", FAKETIME_SAVE_FAIL_OPEN="1",
                        FAKETIME_SAVE_MAX_BYTES="1048576",
                        FAKETIME_SAVE_FILE=str(self.output),
                        FAKETIME_SHARED=f"/faketime_sem_{suffix} /faketime_shm_{suffix}")

    def run_probe(self, *args, expect=7):
        result = subprocess.run([self.probe, *args], env=self.env,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, expect, result.stderr + result.stdout)
        if expect == 7:
            self.assertTrue(result.stdout.endswith("business finished\n"), result.stdout)
            self.assertEqual(result.stderr, "")
        else:
            self.assertIn("opening file for saving timestamps failed", result.stderr)
        return result.stdout

    def errors(self):
        return struct.unpack_from("<Q", self.shared.read_bytes(), 88)[0]

    def test_record_replay(self):
        want = self.run_probe()
        self.assertEqual(self.output.stat().st_size, 4 * 16)
        self.assertEqual(self.errors(), 0)
        del self.env["FAKETIME_SAVE_FILE"]
        self.env["FAKETIME_LOAD_FILE"] = str(self.output)
        self.assertEqual(self.run_probe(), want)

    def test_open_failure_and_next_process_stays_disabled(self):
        self.env["FAKETIME_SAVE_FILE"] = str(self.output / "missing" / "time.bin")
        self.run_probe()
        self.assertEqual(self.errors(), 1)
        self.env["FAKETIME_SAVE_FILE"] = str(self.output)
        self.run_probe()
        self.assertEqual(self.output.stat().st_size, 0)

    def test_budget_preserves_business_and_latches_across_processes(self):
        self.env["FAKETIME_SAVE_MAX_BYTES"] = "16"
        self.run_probe()
        self.assertEqual(self.output.stat().st_size, 16)
        self.assertEqual(self.errors(), 1)
        self.run_probe()
        self.assertEqual(self.output.stat().st_size, 16)

    def test_write_failure_can_leave_a_structurally_valid_file(self):
        self.run_probe("limit-file-size")
        self.assertEqual(self.output.stat().st_size, 16)
        self.assertEqual(self.errors(), 1)
        # Complete timestamp bytes alone do NOT establish a complete recording.
        sec, nsec = struct.unpack(">qq", self.output.read_bytes())
        self.assertGreater(sec, 0)
        self.assertTrue(0 <= nsec < 1_000_000_000)

    def test_lock_failure(self):
        self.env["LD_PRELOAD"] += ":" + self.fault
        self.env["TEST_FLOCK_FAIL"] = "lock"
        self.run_probe()
        self.assertEqual(self.errors(), 1)
        self.assertEqual(self.output.stat().st_size, 0)

    def test_unlock_failure(self):
        self.env["LD_PRELOAD"] += ":" + self.fault
        self.env["TEST_FLOCK_FAIL"] = "unlock"
        self.run_probe()
        self.assertEqual(self.errors(), 1)

    def test_invalid_budget_disables_recording(self):
        self.env["FAKETIME_SAVE_MAX_BYTES"] = "invalid"
        self.run_probe()
        self.assertEqual(self.errors(), 1)
        self.assertEqual(self.output.stat().st_size, 0)

    def test_default_open_failure_still_exits(self):
        del self.env["FAKETIME_SAVE_FAIL_OPEN"]
        self.env["FAKETIME_SAVE_FILE"] = str(self.output / "missing" / "time.bin")
        self.run_probe(expect=1)

    def test_default_recording_ignores_optional_budget(self):
        del self.env["FAKETIME_SAVE_FAIL_OPEN"]
        self.env["FAKETIME_SAVE_MAX_BYTES"] = "16"
        self.run_probe()
        self.assertEqual(self.output.stat().st_size, 4 * 16)
        self.assertEqual(self.errors(), 0)


if __name__ == "__main__":
    unittest.main()
