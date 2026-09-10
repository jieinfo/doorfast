#!/usr/bin/env python3
"""Compare operator-collected inventories without executing system commands."""
import json
import pathlib
import sys


def load(directory, name):
    path = directory / name
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 1048576:
        raise ValueError(f"invalid inventory file: {name}")
    return path.read_text()


def compare(before, after, roles):
    failures = []
    observations = {}
    def reject(condition, reason):
        if condition:
            failures.append(reason)
    for directory in (before, after):
        manifest = json.loads(load(directory, "manifest.json"))
        reject(manifest != {"schema_version": 1, "query_failed": 0}, "incomplete_inventory")
    required = ("bridge", "upstream", "downstream", "management")
    if set(roles) != set(required) or any(not isinstance(roles[k], str) or not roles[k] for k in required):
        raise ValueError("four explicit interface roles required")
    if len(set(roles.values())) != 4:
        raise ValueError("interface roles must be distinct")
    reject(load(before, "hashes.txt") != load(after, "hashes.txt"), "configuration_changed")
    reject(load(after, "passive.txt").strip() != "1", "not_passive")
    disk = load(after, "disk.txt").splitlines()
    if len(disk) != 2:
        raise ValueError("invalid disk inventory")
    disk_fields = disk[1].split()
    if len(disk_fields) != 6 or disk_fields[-1] != "/mnt/doorfast":
        raise ValueError("invalid evidence mount")
    reject(int(disk_fields[3]) < 6144 * 1024, "low_space")
    def stable(value):
        if isinstance(value, dict):
            return {k: stable(v) for k, v in value.items() if k not in
                    {"packets", "bytes", "expires", "cacheinfo"}}
        if isinstance(value, list):
            return [stable(v) for v in value]
        return value
    for name in ("routes.json", "firewall.json"):
        reject(stable(json.loads(load(before, name))) != stable(json.loads(load(after, name))), name + "_changed")
    links = {v["ifname"]: v for v in json.loads(load(after, "links.json"))}
    old = {v["ifname"]: v for v in json.loads(load(before, "links.json"))}
    for role in required:
        reject(roles[role] not in links, role + "_missing")
    bridge = links.get(roles["bridge"], {})
    members = {k for k, v in links.items() if v.get("master") in
               (roles["bridge"], bridge.get("ifindex", -1))}
    reject(members != {roles["upstream"], roles["downstream"]}, "bridge_members")
    for item in json.loads(load(after, "addresses.json")):
        if item["ifname"] == roles["bridge"]:
            reject(bool(item.get("addr_info")), "bridge_address")
    for role in ("upstream", "downstream", "management"):
        name = roles[role]
        link = links.get(name, {})
        reject("LOWER_UP" not in link.get("flags", []), role + "_no_carrier")
        for direction in ("rx", "tx"):
            new_stats = link.get("stats64", {}).get(direction, {})
            old_stats = old.get(name, {}).get("stats64", {}).get(direction, {})
            for key in ("errors", "dropped"):
                a, b = old_stats.get(key), new_stats.get(key)
                reject(type(a) is not int or type(b) is not int or b != a,
                       f"{role}_{direction}_{key}")
            observations[f"{role}_{direction}_packets"] = new_stats.get("packets")
    report = json.loads(load(after, "preflight.json"))
    reject(report.get("safe") is not True, "preflight_unsafe")
    return {"safe": not failures, "failures": sorted(set(failures)), "observations": observations}


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: compare-doorfast-site.py BEFORE AFTER ROLES.json")
    try:
        roles = json.loads(pathlib.Path(sys.argv[3]).read_text())
        result = compare(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), roles)
        print(json.dumps(result, sort_keys=True))
        return 0 if result["safe"] else 2
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({"safe": False, "error": str(error)}))
        return 1


if __name__ == "__main__":
    sys.exit(main())
