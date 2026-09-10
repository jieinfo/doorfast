import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class VmPreflightStateTest(unittest.TestCase):
    def test_bridge_runtime_timer_does_not_look_like_a_network_change(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            fake_ssh = root / "ssh.sh"
            shutil.copyfile("tests/fixtures/fake-vm-preflight-ssh.sh", fake_ssh)
            fake_ssh.chmod(0o700)
            environment = os.environ.copy()
            environment["DOORFAST_FAKE_COUNTER"] = str(root / "counter")

            result = subprocess.run(
                [
                    "python3",
                    "-B",
                    "tests/run_doorfast_vm_preflight.py",
                    str(fake_ssh),
                ],
                text=True,
                capture_output=True,
                env=environment,
            )

            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("remained read-only", result.stdout)


if __name__ == "__main__":
    unittest.main()
