import importlib.util
import io
import os
from pathlib import Path
import signal
import stat
import subprocess
import tarfile
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "doorfast_pcm_vm_runner", Path(__file__).with_name("run_doorfast_vm_pcm_http.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


def archive(runner_source):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w") as bundle:
        for name, contents, mode in (
                ("doorfast-pcm-http-acceptance", b"#!/bin/sh\nexit 0\n", 0o700),
                ("runner.py", runner_source, 0o600)):
            entry = tarfile.TarInfo(name)
            entry.size = len(contents)
            entry.mode = mode
            bundle.addfile(entry, io.BytesIO(contents))
    return output.getvalue()


class RemoteLifecycleTests(unittest.TestCase):
    def test_remote_owner_removes_tree_after_hup(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            wrapper = base / "ssh-wrapper"
            wrapper.write_text("#!/bin/sh\nexec /bin/sh -c \"$1\"\n")
            wrapper.chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
            observed = base / "remote-root"
            source = b"""import os
from pathlib import Path
import signal
import sys
import time
Path(os.environ['DF_PCM_TEST_ROOT']).write_text(str(Path(sys.argv[2]).parent))
os.kill(os.getppid(), signal.SIGHUP)
time.sleep(0.05)
"""
            previous = os.environ.get("DF_PCM_TEST_ROOT")
            os.environ["DF_PCM_TEST_ROOT"] = str(observed)
            try:
                with self.assertRaises(subprocess.CalledProcessError):
                    RUNNER.remote_acceptance(str(wrapper), archive(source), timeout=5)
            finally:
                if previous is None:
                    os.environ.pop("DF_PCM_TEST_ROOT", None)
                else:
                    os.environ["DF_PCM_TEST_ROOT"] = previous
            remote_root = Path(observed.read_text())
            self.assertFalse(remote_root.exists(), f"remote tree remained: {remote_root}")


if __name__ == "__main__":
    unittest.main()
