#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
"""Assert that AVX2 instructions exist in exactly one object file.

`/arch:AVX2` is a per-TRANSLATION-UNIT flag, not a per-function one. Everything
compiled into that unit gets AVX2 instructions — an inline function pulled in from
a header, a container's growth path, a <cmath> helper — and the linker's COMDAT
folding is then free to splice that copy into a caller in a different unit, one
that never ran the CPU probe.

The result is an illegal-instruction crash on a pre-Haswell machine, in code that
looks entirely unrelated to SIMD, and it does not reproduce on any machine new
enough to have been used to build it. It is the single most expensive mistake
available in this area, and it is silent until a user hits it.

KernelsAvx2.cpp defends itself by including only <immintrin.h> and Kernels.h and
defining exactly one function; the build defends it by giving the flag to nothing
else. This checks that both actually held, which is the only claim that matters.

Usage:
    python tools/dev/check_avx2_containment.py [build-dir]
"""
import pathlib
import re
import shutil
import subprocess
import sys

# The 256-bit registers only AVX-family instructions can name. Their presence in an
# object file means that file was compiled with an AVX flag, whatever anyone meant.
YMM = re.compile(rb"\bymm\d", re.IGNORECASE)

ALLOWED = {"kernelsavx2.obj"}

VSWHERE = (r"C:\Program Files (x86)\Microsoft Visual Studio"
           "\\Installer\\vswhere.exe")


def find_dumpbin():
    """dumpbin, from PATH or from the Visual Studio installation.

    CI runners have MSVC but do not put its tools on PATH unless a developer prompt
    is entered first, and requiring that of every caller is how a check ends up
    being run by nobody.
    """
    found = shutil.which("dumpbin")
    if found:
        return found

    vswhere = pathlib.Path(VSWHERE)
    if not vswhere.is_file():
        return None

    result = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
        capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout.strip():
        return None

    root = pathlib.Path(result.stdout.strip())
    candidates = sorted(root.glob("VC/Tools/MSVC/*/bin/Host*/x64/dumpbin.exe"))
    return str(candidates[-1]) if candidates else None


def main() -> int:
    build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build-check")

    dumpbin = find_dumpbin()
    if dumpbin is None:
        if sys.platform == "win32":
            # On Windows this IS the check, and a silent skip is how a guard rots:
            # the job goes green, nobody looks, and the leak ships.
            print("check_avx2_containment: dumpbin not found on a Windows build. "
                  "It ships with MSVC; run from a developer prompt, or ensure "
                  "vswhere.exe is present.", file=sys.stderr)
            return 1

        # Elsewhere the containment is enforced by -mavx2 being set on the one
        # source file, and objdump would be the equivalent check. Nothing to do.
        print("check_avx2_containment: not an MSVC build; skipped")
        return 0

    objs = sorted(build.rglob("caecilia_core.dir/**/*.obj"))
    if not objs:
        print(f"check_avx2_containment: no objects under {build}", file=sys.stderr)
        return 1

    offenders = []
    found_in_allowed = False

    for obj in objs:
        result = subprocess.run([dumpbin, "/nologo", "/disasm", str(obj)],
                                capture_output=True)
        if result.returncode != 0:
            print(f"check_avx2_containment: dumpbin failed on {obj.name}", file=sys.stderr)
            return 1

        if YMM.search(result.stdout):
            if obj.name.lower() in ALLOWED:
                found_in_allowed = True
            else:
                offenders.append(obj.name)

    if offenders:
        print("check_avx2_containment: AVX2 leaked into "
              f"{len(offenders)} object(s) that no CPU probe guards:", file=sys.stderr)
        for name in offenders:
            print(f"  {name}", file=sys.stderr)
        print("These will execute on any CPU that loads the plugin, including one "
              "without AVX2, where they are an illegal instruction.", file=sys.stderr)
        return 1

    if not found_in_allowed:
        # Not a leak, but not right either: the AVX2 kernel is not being built, so
        # every machine is silently running the four-wide path.
        print("check_avx2_containment: no AVX2 anywhere -- KernelsAvx2.cpp did not "
              "get its flag, so the eight-wide path is dead code", file=sys.stderr)
        return 1

    print(f"check_avx2_containment: {len(objs)} objects, AVX2 confined to "
          f"{', '.join(sorted(ALLOWED))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
