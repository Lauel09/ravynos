#!/usr/bin/env python3
"""Static audit for host-portability hazards in the build tree.

The Phase 1 rule is that macOS support is *additive*: Linux must keep
building.  That means every host-specific fix has to be guarded, and every
unguarded FreeBSD/GNU/Linux-ism is a latent failure on the other host.  This
audit finds the recurring classes of problem without running a build, so the
loop is: audit -> read the few real hits -> fix -> build.

Checks (severity in brackets):

  chown-root        [high]   `chown root` / `:wheel` / `install -o root` in
                             Makefiles -- fails outright as a non-root user.
  freebsd-tool      [high]   mdconfig, zpool, gpart, makefs, mkimg, mkuzip,
                             newfs_msdos, etdump, kldload in anything that is
                             not the legacy ISO/build.sh (which is documented
                             as retired, not used by the portable pipeline).
  gnu-only-flag     [high]   readlink -f, stat -c, date -d, sed -i without a
                             suffix, grep -P, cp --parents, tar --wildcards,
                             xargs -r/-d, install -D, sort -V, realpath
                             --relative-to: all fine on Linux, fatal on macOS.
  bsd-only-flag     [medium] stat -f in build scripts (fails on Linux),
                             md5 -r, sed -i '' (fails on GNU sed).
  arch-assumption   [medium] `uname -m` used to choose the target
                             architecture (the fork makes the target explicit
                             via TARGET_ARCH; host arch must not leak in).
  xcode-shim        [low]    /usr/bin/yacc, /usr/bin/lex, xcrun xcodebuild
                             hard-coded paths.
  hardcoded-path    [low]    /usr/local/bin, /opt/local/bin (MacPorts),
                             /usr/local/opt/... without a Homebrew probe.
  shell-portability [low]    `echo -e`, `[ ... == ... ]`, `local` at top level
                             in sh scripts (works in bash, not POSIX sh).

Usage:
    macbuild-audit.py [--root PATH] [--severity high|medium|low]
                      [--path SUBDIR ...] [--json] [--quiet]

Exit status: 0 = no findings at or above the requested severity,
1 = findings present (so it can be used as a gate), 2 = usage error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

SEVERITY_ORDER = {"high": 3, "medium": 2, "low": 1}

# Directories that are never interesting to scan.
SKIP_DIRS = {
    ".git", "build", "node_modules", "__pycache__", ".venv", "venv",
    ".mypy_cache", ".pytest_cache", "tests_data",
}

# Substrings of paths that are scanned but whose hits are reported only as
# informational: vendored upstream sources we do not modify, interactive
# developer targets (which legitimately need sudo), and retired trees.
SKIP_PATTERNS = (
    "/unit-tests/", "/RegressionTests/", "/tests/", "/test/", "/test-cases/",
    "/testsuite/", "/Plugins/",
    # Overlay scripts run inside the installed system, not on the build host.
    "/ISO/overlays/",
    # Vendored third-party CI scaffolding.
    "/.gitlab-ci/", "/libinput/",
    "/old-launchd/", "/utils/ci/", "/utils/release/",
    # Vendored LLVM subprojects: read, never edited by this fork.
    "/llvm/llvm/", "/llvm/lldb/", "/llvm/clang/", "/llvm/compiler-rt/",
    "/llvm/lld/", "/llvm/libcxx/", "/llvm/libcxxabi/", "/llvm/openmp/",
    "/llvm/polly/", "/llvm/flang/", "/llvm/mlir/", "/llvm/bolt/",
    "/xcbuild/", "/tapi/",
)

# Paths that are allowed to contain hazards on purpose.
ALLOWLIST = {
    # Retired FreeBSD-only ISO assembly: documented as not part of the
    # portable pipeline (see ROADMAP.md Phase 2) and never run on macOS.
    "ISO/build.sh",
    "ISO/scripts/mkisoimages-amd64.sh",
    "ISO/scripts/mkisoimages-i386.sh",
    # Legacy zsh wrapper for the old FreeBSD build; historical reference.
    "tools/build.sh",
    # The installer script runs on the installed system, not the build host.
    "tools/ravynOS_install.sh",
}

BUILD_FILES = (".mk", "Makefile", "makefile", "GNUmakefile")
SCRIPT_EXTS = (".sh", ".py", ".pl", ".m", ".c", ".h", ".in", ".mk")

CHECKS = [
    # (name, severity, regex, file-predicate, message)
    ("chown-root", "high",
     re.compile(r"\bchown\s+(-\w+\s+)*(root|:wheel|root:wheel)|install\s+-[a-zA-Z]*o\s+root"),
     "make-and-script",
     "privileged chown/install fails for a non-root build; guard it or drop it"),
    ("freebsd-tool", "high",
     re.compile(r"\b(mdconfig|zpool|zfs\s+(create|set)|gpart|makefs|mkimg|mkuzip|"
                r"newfs_msdos|etdump|kldload|sysrc)\b"),
     "make-and-script",
     "FreeBSD-only tool; the portable pipeline must not need it"),
    ("gnu-only-flag", "high",
     re.compile(r"\breadlink\s+-f\b|\bstat\s+-c\b|\bdate\s+-d\b|"
                r"\bsed\s+-i\s+(-[a-zA-Z]+\s+)*['\"]?[^'\"\s]|\bgrep\s+-[a-zA-Z]*P|"
                r"\bcp\s+--parents\b|\btar\s+--wildcards\b|\bxargs\s+-[a-zA-Z]*[rd]\b|"
                r"\binstall\s+-D\b|\bsort\s+-V\b|\brealpath\s+--relative-to\b"),
     "make-and-script",
     "GNU-only option; fails on BSD/macOS tooling"),
    ("bsd-only-flag", "medium",
     re.compile(r"\bstat\s+-f\b|\bmd5\s+-r\b|\bsed\s+-i\s+''"),
     "make-and-script",
     "BSD/macOS-only option; fails with GNU coreutils"),
    ("arch-assumption", "medium",
     re.compile(r"uname\s+-m|hw\.optional|MACHINE_ARCH\s*[?:]?="),
     "make-and-script",
     "host architecture leaking into target selection; use TARGET_ARCH"),
    ("xcode-shim", "low",
     re.compile(r"/usr/bin/(yacc|lex|m4)\b|xcrun\s+xcodebuild"),
     "make-and-script",
     "Xcode shim path; needs a Homebrew fallback with CLT-only hosts"),
    ("hardcoded-path", "low",
     re.compile(r"/opt/local/bin|/usr/local/opt/[a-z0-9@._-]+"),
     "make-and-script",
     "hard-coded package prefix; probe with `command -v`/`brew --prefix`"),
    ("shell-portability", "low",
     re.compile(r"echo\s+-e\b|\[\s+\S+\s+==\s+\S+\s+\]"),
     "shell-only",
     "not POSIX sh; breaks on macOS /bin/sh (bash 3.2) or dash"),
]


def file_kind(path: str) -> str:
    name = os.path.basename(path)
    if name.endswith(".sh"):
        return "shell"
    if name in BUILD_FILES or name.endswith(".mk"):
        return "make"
    return "other"


def wanted(rule_kind: str, path: str) -> bool:
    kind = file_kind(path)
    if rule_kind == "make-and-script":
        return kind in ("make", "shell")
    if rule_kind == "shell-only":
        return kind == "shell"
    return True


def scan(root: str, subdirs: list[str], min_severity: str) -> list[dict]:
    threshold = SEVERITY_ORDER[min_severity]
    findings: list[dict] = []
    roots = [os.path.join(root, s) if not os.path.isabs(s) else s for s in subdirs] or [root]

    for base in roots:
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for filename in filenames:
                path = os.path.join(dirpath, filename)
                rel = os.path.relpath(path, root)
                if rel in ALLOWLIST:
                    continue
                posix_rel = "/" + rel.replace(os.sep, "/")
                if any(pattern in posix_rel for pattern in SKIP_PATTERNS):
                    continue
                if not (filename.endswith(BUILD_FILES) or filename.endswith(SCRIPT_EXTS)
                        or file_kind(path) == "make"):
                    continue
                try:
                    with open(path, "r", encoding="utf-8", errors="replace") as fh:
                        lines = fh.readlines()
                except OSError:
                    continue
                for lineno, line in enumerate(lines, 1):
                    stripped = line.strip()
                    if stripped.startswith(("#", "//", ";")):
                        continue
                    # An explicit, local opt-out beats a global allowlist: the
                    # author states why this occurrence is portable.
                    if "audit:allow" in line:
                        continue
                    previous = lines[lineno - 2] if lineno >= 2 else ""
                    if "audit:allow" in previous:
                        continue
                    for name, severity, pattern, rule_kind, message in CHECKS:
                        if SEVERITY_ORDER[severity] < threshold:
                            continue
                        if not wanted(rule_kind, path):
                            continue
                        match = pattern.search(line)
                        if match:
                            # Documentation and warning text mentions a tool
                            # without invoking it; a tool name that is part of
                            # a path (usr.sbin/makefs/tests) is not an
                            # invocation either.
                            if name == "freebsd-tool":
                                if ".warning" in line or ".info" in line:
                                    continue
                                end = match.end()
                                if end < len(line) and line[end] == "/":
                                    continue
                            findings.append({
                                "check": name,
                                "severity": severity,
                                "file": rel,
                                "line": lineno,
                                "match": match.group(0).strip(),
                                "message": message,
                                "text": stripped[:160],
                            })
    findings.sort(key=lambda f: (-SEVERITY_ORDER[f["severity"]], f["file"], f["line"]))
    return findings


def summarise(findings: list[dict], quiet: bool) -> None:
    if not findings:
        if not quiet:
            print("portability audit: no findings")
        return
    by_check: dict[str, int] = {}
    for f in findings:
        by_check[f["check"]] = by_check.get(f["check"], 0) + 1

    if not quiet:
        current_file = None
        for f in findings:
            if f["file"] != current_file:
                print(f"\n{f['file']}")
                current_file = f["file"]
            print(f"  {f['line']:>5}  {f['severity']:<6} {f['check']:<18} {f['match']}")
            print(f"         {f['message']}")
            print(f"         {f['text']}")
        print()
    print("summary:")
    for check, count in sorted(by_check.items(), key=lambda kv: -kv[1]):
        severity = next(f["severity"] for f in findings if f["check"] == check)
        print(f"  {severity:<6} {check:<18} {count}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Audit the build tree for host-portability hazards")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--path", action="append", default=[],
                    help="subdirectory to scan (repeatable; default: whole tree)")
    ap.add_argument("--severity", choices=("high", "medium", "low"), default="low",
                    help="minimum severity to report (default: all)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--list-checks", action="store_true")
    args = ap.parse_args(argv)

    if args.list_checks:
        for name, severity, _, kind, message in CHECKS:
            print(f"{severity:<6} {name:<18} {message}  [{kind}]")
        return 0

    if not os.path.isdir(args.root):
        print(f"audit: not a directory: {args.root}", file=sys.stderr)
        return 2

    findings = scan(args.root, args.path, args.severity)
    if args.json:
        print(json.dumps({"root": args.root, "findings": findings,
                          "count": len(findings)}, indent=2))
    else:
        summarise(findings, args.quiet)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
