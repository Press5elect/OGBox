#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
"""OGBox - the licences of every Rust crate in a static library, for the title's notices.

    rust-licences.py <crate checkout> <toolchain> <cargo target dir>  > THIRD-PARTY.txt

The crates are the ones the build compiled for the console: every source file named in the
target's dependency files (*.d), so the standard library's own crates (core, alloc, std and the
crates.io crates std is made of) are in, and the build's tools (build scripts, proc macros) are
not. For each: its name, version, licence (its Cargo.toml) and the text of each licence file it
carries; the Rust project's own crates by the toolchain's COPYRIGHT and licence texts.
"""
import re
import subprocess
import sys
from pathlib import Path


def toml_field(text: str, field: str) -> str:
    m = re.search(rf'^{field}\s*=\s*"([^"]*)"', text, re.M)
    return m.group(1) if m else ""


def main() -> int:
    checkout, toolchain, target = Path(sys.argv[1]).resolve(), sys.argv[2], Path(sys.argv[3]).resolve()
    sysroot = Path(subprocess.run(["rustc", f"+{toolchain}", "--print", "sysroot"], check=True,
                                  capture_output=True, text=True).stdout.strip())
    sources = set()
    for dep in target.rglob("*.d"):
        rel = dep.relative_to(target).parts
        # the console's are all under <target>/<triple>/ (deps/, or build/<crate>/<hash>/out/ with
        # cargo's build-dir layout), the standard library's included; the host's (build scripts,
        # proc macros) are under <target>/release/
        if len(rel) < 2 or not rel[0].endswith("unknown-freebsd"):
            continue
        sources.update(re.findall(r"(/\S+?\.rs)", dep.read_text()))
    registry, rust_own, local = {}, set(), set()
    for path in sources:
        m = re.search(r"/registry/src/[^/]+/([^/]+)/", path)
        if m:
            registry[m.group(1)] = Path(path[:m.end()])
            continue
        m = re.search(r"/lib/rustlib/src/rust/library/([^/]+)/", path)
        if m:
            rust_own.add(m.group(1))
            continue
        if path.startswith(str(checkout)):
            m = re.search(r"/crates/([^/]+)/", path)
            local.add(m.group(1) if m else "?")
    out = []
    out.append("The Rust crates in libdsp56300_emu_ffi.a (OGBox's DSP56300 JIT), as built for the console.\n")
    out.append(f"Toolchain: {toolchain}.\n\n")
    licence = checkout / "LICENSE"
    out.append(f"== dsp56300 ({', '.join(sorted(local))}): MIT, Copyright (c) Matt Borgerson; with OGBox's patch "
               "(ps5/patches/dsp56300) ==\n\n")
    out.append(licence.read_text() + "\n")
    docs = sysroot / "share/doc/rust"
    out.append(f"== The Rust standard library ({', '.join(sorted(rust_own))}): MIT OR Apache-2.0, "
               "Copyright The Rust Project Developers ==\n\n")
    out.append("(the in-tree notices of each of its files: COPYRIGHT-library.html in the toolchain's share/doc/rust, "
               "and https://github.com/rust-lang/rust/blob/master/COPYRIGHT-library.html)\n\n")
    for name in ("licenses/MIT.txt", "licenses/Apache-2.0.txt", "licenses/LLVM-exception.txt"):
        f = docs / name
        if f.is_file():
            text = f.read_text()
            if name.endswith(".html"):
                text = re.sub(r"<[^>]+>", "", text)
                text = re.sub(r"\n\s*\n+", "\n\n", text)
            out.append(f"-- {name} --\n{text.strip()}\n\n")
    for crate in sorted(registry, key=str.lower):
        root = registry[crate]
        manifest = (root / "Cargo.toml").read_text(errors="replace")
        name, version = toml_field(manifest, "name"), toml_field(manifest, "version")
        lic = toml_field(manifest, "license") or ("see " + toml_field(manifest, "license-file"))
        authors = re.search(r"^authors\s*=\s*\[([^\]]*)\]", manifest, re.M)
        who = ", ".join(a.strip().strip('"') for a in authors.group(1).split(",") if a.strip()) if authors else ""
        out.append(f"== {name} {version}: {lic}{(', ' + who) if who else ''} ==\n\n")
        texts = sorted(p for p in root.iterdir() if p.is_file() and re.match(r"(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)", p.name, re.I))
        for t in texts:
            out.append(f"-- {t.name} --\n{t.read_text(errors='replace').strip()}\n\n")
        if not texts:
            out.append("(the crate carries no licence file: its licence, named above, is the text of that name in the "
                       "standard library's section)\n\n")
    sys.stdout.write("".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
