#!/usr/bin/env python3
"""Setup, build and package Rez for PortMaster from the repository root.

    python tools/dc.py doctor                  what this machine has / lacks
    python tools/dc.py setup --disc <image>    verify the disc, extract the boot binary and code
                                               overlays into game/extracted/, remember the image
    python tools/dc.py build [-j N]            build host translator, cross-compile aarch64 binary,
                                               and package build/rez.zip
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(cmd: list[str], cwd: str | None = None, check: bool = True, env: dict | None = None) -> int:
    print("+ " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    r = subprocess.run(cmd, cwd=cwd, env=env or os.environ)
    if check and r.returncode != 0:
        sys.exit(f"command failed with exit code {r.returncode}")
    return r.returncode


def port_info() -> dict:
    port = ROOT
    tomls = glob.glob(os.path.join(port, "game", "*.toml"))
    tomls = [t for t in tomls if not t.endswith("suggested.toml")]
    if len(tomls) != 1:
        sys.exit(f"{port}: expected exactly one game/<id>.toml, found {len(tomls)}")
    gid = os.path.splitext(os.path.basename(tomls[0]))[0]
    local_path = os.path.join(port, ".rez_recompiled", "local.json")
    local = json.load(open(local_path)) if os.path.exists(local_path) else {}
    return {"dir": port, "id": gid, "toml": tomls[0], "local": local, "local_path": local_path}


def save_local(info: dict) -> None:
    os.makedirs(os.path.dirname(info["local_path"]), exist_ok=True)
    with open(info["local_path"], "w") as f:
        json.dump(info["local"], f, indent=2)


def cmd_doctor(_a) -> int:
    rows = []

    def have(name, ok, note=""):
        rows.append((name, "ok" if ok else "MISSING", note))

    for tool in ("cmake", "ninja", "clang", "clang++", "ld.lld"):
        have(tool, shutil.which(tool) is not None)

    container_tool = shutil.which("podman") or shutil.which("docker")
    have("podman/docker", container_tool is not None, container_tool or "needed by tools/toolchain.sh")

    tc_dir = os.path.join(ROOT, "build", "toolchain")
    tc_ok = os.path.isdir(os.path.join(tc_dir, "aarch64-nextui-linux-gnu", "libc"))
    have("toolchain (build/toolchain)", tc_ok, "sh tools/toolchain.sh")

    w = max(len(r[0]) for r in rows)
    for name, st, note in rows:
        print(f"{name:<{w}}  {st:<7}  {note}")
    return 0 if all(r[1] == "ok" for r in rows) else 1


def cmd_setup(a) -> int:
    info = port_info()
    disc = os.path.abspath(a.disc)
    from_toml = open(info["toml"], encoding="utf-8").read()
    want = None
    for line in from_toml.splitlines():
        if line.strip().startswith("sha1_1st_read"):
            want = line.split("=", 1)[1].split("#")[0].strip().strip('"')
    # Only the boot executable is extracted: the translator needs it at build time, and at run
    # time everything else is read off the disc image itself.
    sys.path.insert(0, os.path.join(ROOT, "engine", "tools", "dcdisc"))
    from dcdisc import open_image
    from dcdisc.ipbin import parse_ipbin, read_ipbin
    from dcdisc.iso9660 import Iso9660
    from dcdisc.scramble import descramble
    # The images the config accepts ([[disc.chd]]): when listed, only those, checked by the SHA-1
    # the CHD's header records, then the whole image's data against the header.
    import tomllib
    disc_cfg = tomllib.loads(from_toml).get("disc", {})
    accepted = [(e["sha1"].lower(), e.get("name", "")) for e in disc_cfg.get("chd", [])]
    if disc_cfg.get("chd_sha1"):
        accepted.append((disc_cfg["chd_sha1"].lower(), ""))
    if accepted:
        import dcdisc.chd as chd_mod
        from dcdisc.chd import ChdError, data_sha1, read_header
        if chd_mod.load_libchdr() is None:
            bdir = os.path.join(info["dir"], "build", "host")
            if not os.path.exists(os.path.join(bdir, "CMakeCache.txt")):
                cfg = ["cmake", "-S", info["dir"], "-B", bdir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DDREAM_RENDERER=OFF"]
                run(cfg)
            run(["cmake", "--build", bdir, "--target", "chdr", "--parallel"])
            chd_mod._lib_missing = False
        names = "\n".join(f"  {n or '?'}.chd (SHA-1 {s})" for s, n in accepted)
        try:
            header = read_header(disc)
        except (ChdError, OSError) as e:
            sys.exit(f"{disc} is not a supported image ({e}). Rez runs only from:\n{names}")
        got_chd = header.sha1.hex()
        match = next((n or s for s, n in accepted if s == got_chd), None)
        if match is None:
            sys.exit(f"{disc} is not a supported image (CHD SHA-1 {got_chd}). Rez runs only from:\n{names}")
        print(f"disc: {match}; checking the whole image against its header ...")
        if data_sha1(disc) != header.raw_sha1:
            sys.exit(f"{disc}: the data does not match its header's checksum; the file is damaged")
        print("disc: image intact")
    # Overlays the config names (`[[relocations]] file = "extracted/fs/<disc path>"`): code the
    # program reads from disc at run time, which the translator needs at build time as well.
    import re
    overlay_paths = sorted(set(re.findall(r'^\s*file\s*=\s*"extracted/fs/([^"]+)"', from_toml, re.M)))
    overlays = {}
    with open_image(disc) as img:
        ip = parse_ipbin(read_ipbin(img))
        iso = Iso9660(img)
        data = iso.read_path(ip.boot_filename)
        for p in overlay_paths:
            overlays[p] = iso.read_path(p)
    if ip.hardware_id.startswith("SEGA SEGAKATANA") is False:
        print(f"warning: IP.BIN hardware id is {ip.hardware_id!r}")
    got = hashlib.sha1(data).hexdigest()
    if want and got != want and hashlib.sha1(descramble(data)).hexdigest() == want:
        data, got = descramble(data), want  # MIL-CD style scrambled boot file
    print(f"{ip.title} {ip.product_number} {ip.product_version}: boot file {ip.boot_filename}, "
          f"{len(data)} bytes")
    # Checked before anything is written, so a disc of another release leaves the existing
    # extraction and remembered disc as they were.
    if want and got != want:
        sys.exit(f"boot binary SHA-1 {got} does not match the config ({want}): wrong release or revision; "
                 "nothing was written")
    print(f"boot binary verified: sha1 {got}")
    out = os.path.join(info["dir"], "game", "extracted", "fs")
    os.makedirs(out, exist_ok=True)
    boot = os.path.join(out, "1ST_READ.BIN")

    def write_if_changed(dst: str, blob: bytes) -> None:
        # An unchanged file keeps its time stamp, so the build does not translate again.
        if os.path.exists(dst) and open(dst, "rb").read() == blob:
            return
        with open(dst, "wb") as f:
            f.write(blob)

    write_if_changed(boot, data)
    print(f"boot file -> {boot}")
    for p, blob in overlays.items():
        dst = os.path.join(out, *p.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        write_if_changed(dst, blob)
    if overlays:
        print(f"{len(overlays)} overlay file(s) named in the config extracted to {out}")
    info["local"]["disc"] = disc
    save_local(info)
    print(f"disc path remembered in {info['local_path']}")
    return 0


def cmd_build(a) -> int:
    build_sh = os.path.join(ROOT, "tools", "build.sh")
    package_sh = os.path.join(ROOT, "tools", "package.sh")
    cmd = ["sh", build_sh]
    if a.jobs:
        cmd += [f"-j{a.jobs}"]
    if a.extra:
        cmd += a.extra
    run(cmd)
    run(["sh", package_sh])
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("doctor").set_defaults(fn=cmd_doctor)
    s = sub.add_parser("setup")
    s.add_argument("--disc", required=True)
    s.set_defaults(fn=cmd_setup)
    b = sub.add_parser("build")
    b.add_argument("-j", "--jobs", type=int, help="parallel jobs for ninja")
    b.add_argument("extra", nargs="*", help="extra arguments passed to build.sh / ninja")
    b.set_defaults(fn=cmd_build)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
