#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
# Crux2 - the notices a title folder carries, and the check before one leaves the machine.
#
# PSHakuX: Crux2's ps5/tools/stage-notices.py (08c3507) for PSHakuX: hakuX's parts and pins, and the
# Xbox's files that must never be in a package. PSHakuX by Press5elect.
#
#   ps5/tools/stage-notices.py dist/PPSA99251            write LEGAL.txt, NOTICE.txt and licenses/
#   ps5/tools/stage-notices.py --check dist/PPSA99251    check a title folder (or an unpacked ZIP)
#
# Staging (tools/link-title.sh runs it) writes, from ps5/notices/components.json:
#   LEGAL.txt                 no piracy, the licence, the credit, unofficial, trademarks
#   NOTICE.txt                PSHakuX's copyright (Press5elect) and its attribution terms
#   licenses/README.txt       the same notice, then every part with its author and licence
#   licenses/components.json  every part: licence, copyright, modifications, the revision and
#                             address of its source, and the files it is in with their sha256
#   licenses/*                the licence texts the parts require
#
# The check is the gate for anything that is passed on. It fails when
#   - a file that must never be in a package is there: a disc image, an XBE, the Xbox's BIOS,
#     MCPX boot ROM, EEPROM or hard disk image, a key, a save or savestate, a console's own
#     settings, logs or address files (boot.txt, props.txt, network.cfg, .env);
#   - an executable file is not covered by a part of components.json;
#   - a notice or a licence text is missing, or a file changed since the notices were written;
#   - a part's source revision is unknown, or a part was built from a tree with local changes.
#
# The shape is PS5_RetroArch's tools/stage-notices.py and check-notices.py (Mihawk-99), made for
# one emulator by Crux2.
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PS5 = Path(__file__).resolve().parent.parent
ROOT = PS5.parent

# Never in a package. (The names are matched without regard to case.)
FORBIDDEN_SUFFIXES = {
    ".iso", ".xiso", ".cci", ".cso", ".chd", ".img", ".qcow2", ".vhd", ".raw", ".gz", ".7z", ".rar",  # disc and disk images
    ".xbe", ".xex",                                                                               # Xbox executables (games)
    ".rom", ".eeprom", ".pup", ".bios",                                                           # BIOS, boot ROM, firmware
    ".key", ".keys", ".xsv", ".sav", ".state", ".hxs",                                            # keys, saves, savestates
    ".log", ".env", ".ini", ".cfg",                                                               # a console's own files
}
FORBIDDEN_NAMES = {"boot.txt", "props.txt", "network.cfg", "test-run.txt", ".env", "eeprom.bin",
                   "mcpx_1.0.bin", "complex_4627.bin"}
FORBIDDEN_FOLDERS = {"hdd", "cache", "saves", "states", "logs", "snaps", "games", "hle", "partition0", "partition1",
                     "partition2", "partition3", "partition4", "partition5"}
ALLOWED_BIN = {"eboot.bin"}
EXECUTABLE_SUFFIXES = {".bin", ".prx", ".elf", ".sprx", ".self", ".so", ".xbe"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def git_revision(directory: Path) -> str:
    try:
        revision = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], check=True,
                                  capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(directory), "status", "--porcelain", "--untracked-files=no"],
                               check=True, capture_output=True, text=True).stdout.strip()
        return revision + (" (with local changes)" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def read(path: Path, default="unknown") -> str:
    return path.read_text().strip() if path.is_file() else default


def pins() -> dict:
    """The revisions the '@name' sources stand for."""
    vulkan = ROOT.parent / "PS5_Vulkan"
    provenance = read(vulkan / ".deps/native/radv-crux2/PROVENANCE.txt", "") or \
        read(vulkan / ".deps/native/radv-release/PROVENANCE.txt", "")
    mesa = next((line.split()[3] for line in provenance.splitlines() if line.startswith("Crux2: PS5 Mesa ")), "") or \
        next((line.split(": ", 1)[1] for line in provenance.splitlines() if line.startswith("revision: ")), "unknown")
    patches = re.search(r"with (ps5/patches/[\w.-]+)", provenance)
    mesa_patches = patches.group(1) if patches else "ps5/patches/mesa-27"
    # The hakuX source this build compiled (tools/prepare-hakux.sh records it).
    build = Path(os.environ.get("PSHAKUX_BUILD_DIR", str(ROOT / "build/ps5")))
    source = read(build.parent / "hakux-src/.pshakux-source.txt", "")
    hakux_tree = next((line.split(": ", 1)[1] for line in source.splitlines() if line.startswith("source: ")), "")
    hakux_rev = next((line.split(": ", 1)[1] for line in source.splitlines() if line.startswith("revision: ")), "unknown")
    patch_names = ", ".join(sorted(p.name for p in (PS5 / "patches/hakux").glob("*.patch")))
    if hakux_tree and Path(hakux_tree).resolve() != (ROOT / "vendor/hakuX").resolve():
        hakux = f"{git_revision(Path(hakux_tree))} (the work tree {hakux_tree}) + those of ps5/patches/hakux/ that apply"
    else:
        hakux = f"{hakux_rev} + ps5/patches/hakux/ ({patch_names})"
    deps = ROOT / ".deps/src"
    return {
        "@hakux": hakux,
        "@sdk": read(ROOT / ".deps/native/ps5-payload-sdk/.ps5-sdk-revision"),
        "@vulkan": git_revision(vulkan),
        "@mesa": mesa + " + Crux2's " + mesa_patches + "/ (" +
                 ", ".join(sorted(p.stem for p in (ROOT / mesa_patches).glob("*.patch"))) + ")",
        "@template": "91179df760df1e6c6f2d4d172bc435319d79ff3c (the foundation this title was made from)",
        "@identity": read(build / "build-identity.txt"),
        "@libslirp": read(deps / "libslirp/.pshakux-revision"),
        "@nv2a_vsh_cpu": read(deps / "nv2a_vsh_cpu/.pshakux-revision"),
        "@xxhash": read(deps / "xxhash/.pshakux-revision"),
        "@libnfs": read(deps / "libnfs/.pshakux-revision"),
    }


def artifact_record(app: Path, name: str) -> dict:
    path = app / name
    if name.endswith("/"):
        files = sorted(p for p in path.rglob("*") if p.is_file())
        digest = hashlib.sha256("".join(f"{sha256(p)}  {p.relative_to(app).as_posix()}\n" for p in files).encode())
        return {"path": name, "files": len(files), "sha256_of_listing": digest.hexdigest()}
    return {"path": name, "sha256": sha256(path)}


def stage(app: Path) -> int:
    components = json.loads((PS5 / "notices/components.json").read_text())["components"]
    resolved = pins()
    licenses = app / "licenses"
    if licenses.exists():
        shutil.rmtree(licenses)
    licenses.mkdir(parents=True)

    legal = (PS5 / "package/LEGAL.txt").read_text()
    (app / "LEGAL.txt").write_text(legal)
    # The copyright and the attribution terms of Press5elect's parts (GPL-3.0 section 7(b)).
    shutil.copyfile(ROOT / "NOTICE", app / "NOTICE.txt")

    lines = [legal.rstrip(), "", "=" * 92, "THE PARTS", "=" * 92, ""]
    records = []
    for component in components:
        texts = []
        for name, source in component["texts"].items():
            source_path = Path(source) if source.startswith("/") else (ROOT / source)
            if not source_path.is_file():
                print(f"{component['id']}: licence text missing: {source}", file=sys.stderr)
                return 1
            shutil.copyfile(source_path, licenses / name)
            texts.append(name)
        revision = component["source"]["revision"]
        revision = resolved.get(revision, revision)
        missing = [name for name in component["artifacts"] if not (app / name).exists()]
        if missing:
            print(f"{component['id']}: not in the title folder: {', '.join(missing)}", file=sys.stderr)
            return 1
        record = {
            "id": component["id"], "name": component["name"], "licence": component["licence"],
            "copyright": component["copyright"], "modifications": component["modifications"], "texts": texts,
            "source": {"url": component["source"]["url"], "revision": revision},
            "artifacts": [artifact_record(app, name) for name in component["artifacts"]],
        }
        records.append(record)
        lines += [component["name"], f"    licence:   {component['licence']}", f"    copyright: {component['copyright']}",
                  f"    source:    {component['source']['url']}", f"    revision:  {revision}"]
        if component["modifications"] not in ("none", "-"):
            lines.append(f"    changed:   {component['modifications']}")
        if texts:
            lines.append(f"    texts:     {', '.join(texts)}")
        lines.append("")

    (licenses / "README.txt").write_text("\n".join(lines))
    (licenses / "components.json").write_text(json.dumps(
        {"title": app.name, "build_identity": resolved["@identity"], "components": records}, indent=2) + "\n")
    print(f"notices: {len(records)} parts, {len(list(licenses.iterdir()))} files in {licenses.relative_to(app.parent)}")
    return 0


def check(app: Path) -> int:
    problems = []
    for path in sorted(app.rglob("*")):
        relative = path.relative_to(app)
        parts = [part.lower() for part in relative.parts]
        if path.is_dir():
            if len(parts) == 1 and parts[0] in FORBIDDEN_FOLDERS:
                problems.append(f"a console's own folder is in the package: {relative}/")
            continue
        name, suffix = parts[-1], path.suffix.lower()
        shipped_text = parts[0] == "licenses" and suffix in {".ini", ".cfg"}
        if name in FORBIDDEN_NAMES or (suffix == ".bin" and name not in ALLOWED_BIN) or \
                (suffix in FORBIDDEN_SUFFIXES and not shipped_text):
            problems.append(f"must never be in a package: {relative}")

    for required in ("LEGAL.txt", "NOTICE.txt", "README.md", "licenses/README.txt", "licenses/components.json",
                     "licenses/GPL-3.0.txt", "eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png",
                     "sce_module/libc.prx"):
        if not (app / required).is_file():
            problems.append(f"missing: {required}")

    manifest = app / "licenses/components.json"
    if manifest.is_file():
        components = json.loads(manifest.read_text())["components"]
        covered_files, covered_folders = set(), []
        for component in components:
            if "unknown" in component["source"]["revision"]:
                problems.append(f"{component['id']}: the revision of its source is unknown")
            if "with local changes" in component["source"]["revision"] or "+dirty" in component["source"]["revision"]:
                problems.append(f"{component['id']}: built from a tree with uncommitted changes")
            for text in component["texts"]:
                if not (app / "licenses" / text).is_file():
                    problems.append(f"{component['id']}: licence text missing: licenses/{text}")
            for artifact in component["artifacts"]:
                path = app / artifact["path"]
                if artifact["path"].endswith("/"):
                    covered_folders.append(artifact["path"])
                    if artifact_record(app, artifact["path"]) != artifact:
                        problems.append(f"{component['id']}: {artifact['path']} changed since the notices were written")
                else:
                    covered_files.add(artifact["path"])
                    if not path.is_file():
                        problems.append(f"{component['id']}: {artifact['path']} is not in the package")
                    elif sha256(path) != artifact["sha256"]:
                        problems.append(f"{component['id']}: {artifact['path']} changed since the notices were written")
        for path in sorted(app.rglob("*")):
            relative = path.relative_to(app).as_posix()
            if path.is_file() and path.suffix.lower() in EXECUTABLE_SUFFIXES and relative not in covered_files and \
                    not any(relative.startswith(folder) for folder in covered_folders):
                problems.append(f"an executable file no part accounts for: {relative}")

    for problem in problems:
        print(f"FAILED: {problem}", file=sys.stderr)
    if not problems:
        print(f"release check: {app} passes ({sum(1 for p in app.rglob('*') if p.is_file())} files)")
    return 1 if problems else 0


def main() -> int:
    arguments = sys.argv[1:]
    checking = bool(arguments) and arguments[0] == "--check"
    if checking:
        arguments = arguments[1:]
    if len(arguments) != 1 or not Path(arguments[0]).is_dir():
        print("usage: stage-notices.py [--check] <title folder>", file=sys.stderr)
        return 2
    app = Path(arguments[0]).resolve()
    return check(app) if checking else stage(app)


if __name__ == "__main__":
    sys.exit(main())
