import importlib.util
import json
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("compare", pathlib.Path(__file__).resolve().parents[1] / "scripts/compare-doorfast-site.py")
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class SiteComparison(unittest.TestCase):
    def test_unchanged_and_failures(self):
        roles = dict(bridge="br-door", upstream="up", downstream="down", management="mgmt")
        with tempfile.TemporaryDirectory() as root:
            before, after = pathlib.Path(root) / "before", pathlib.Path(root) / "after"
            links = []
            for name in roles.values():
                links.append(dict(ifname=name, flags=["LOWER_UP"],
                    master="br-door" if name in ("up", "down") else "",
                    stats64={d: dict(packets=1, errors=0, dropped=0) for d in ("rx", "tx")}))
            for directory in (before, after):
                directory.mkdir()
                for name, value in {
                    "manifest.json": dict(schema_version=1, query_failed=0),
                    "links.json": links, "routes.json": [], "firewall.json": {},
                    "addresses.json": [dict(ifname="br-door", addr_info=[])],
                    "preflight.json": dict(safe=True)
                }.items():
                    (directory / name).write_text(json.dumps(value))
                (directory / "hashes.txt").write_text("same")
                (directory / "passive.txt").write_text("1\n")
                (directory / "disk.txt").write_text("Filesystem 1024-blocks Used Available Capacity Mounted\n/dev/test 33000000 1 30000000 1% /mnt/doorfast\n")
            self.assertTrue(compare.compare(before, after, roles)["safe"])
            links[1]["stats64"]["rx"]["errors"] = 1
            (after / "links.json").write_text(json.dumps(links))
            self.assertIn("upstream_rx_errors", compare.compare(before, after, roles)["failures"])
            (after / "passive.txt").write_text("0")
            self.assertIn("not_passive", compare.compare(before, after, roles)["failures"])
            (after / "manifest.json").write_text("{}")
            self.assertIn("incomplete_inventory", compare.compare(before, after, roles)["failures"])


if __name__ == "__main__":
    unittest.main()
