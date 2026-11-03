#!/bin/sh
# Remote-model loop runner: check out a branch the model pushed, run the
# requested tasks on this Mac, and write one short log to paste back.
#
# Usage: tools/arena-run.sh <branch|.> <task> [<task> ...]
#   <branch>  fetched from origin and checked out (local copy is reset to it);
#             "." runs on the current checkout without fetching.
# Tasks (run in order; a failing task stops the run):
#   loader         build tools/bootloader/BOOTX64.EFI
#   kernel         rebuild XNU and check the staged kernel (darwin-gate.sh xnu)
#   boot[=ARGS]    boot the built kernel in QEMU (tools/xnu-boot.sh ARGS;
#                  separate several ARGS with commas: boot=--secs,40)
#   tests          tools/tests/run-tests.sh
#   selftest       tools/pipeline-selftest.sh --no-qemu
#   audit          tools/macbuild-audit.py --severity high
#   stage=NAME     tools/macbuild.sh --stage NAME (Developer|Kernel|Libraries|BSD|world)
#   world          tools/macbuild.sh --audit --target-arch x86_64 (full build)
#   script=PATH    run a script committed on the branch (sh PATH), for probes
#
# The log goes to out/arena/logs/ and is copied to the clipboard.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# A checkout can replace this file while sh is still reading it; run a copy.
if [ -z "${ARENA_REEXEC:-}" ]; then
    tmp=$(mktemp -t arena-run)
    cp "$0" "$tmp"
    ARENA_REEXEC=1 ARENA_ROOT="$ROOT" exec sh "$tmp" "$@"
fi
ROOT=${ARENA_ROOT:-$ROOT}
cd "$ROOT"

if [ $# -lt 2 ]; then
    sed -n '2,22p' "$0"; exit 2
fi
BRANCH=$1; shift
BASE=origin/fork/darwin-dev

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "arena-run: tracked files have local changes; commit or stash them first" >&2
    git status -s | grep -v '^??' | head >&2
    exit 2
fi

if [ "$BRANCH" != "." ]; then
    git fetch -q origin || { echo "arena-run: git fetch failed" >&2; exit 2; }
    git rev-parse -q --verify "origin/$BRANCH" >/dev/null ||
        { echo "arena-run: origin/$BRANCH does not exist" >&2; exit 2; }
    git checkout -q -B "$BRANCH" "origin/$BRANCH" || exit 2
else
    git fetch -q origin 2>/dev/null || true
    BRANCH=$(git rev-parse --abbrev-ref HEAD)
fi

mkdir -p out/arena/logs
safe=$(printf %s "$BRANCH" | tr -c 'A-Za-z0-9._-' '_')
LOG="$ROOT/out/arena/logs/$(date +%Y%m%d-%H%M%S)-$safe.log"
FULL="$ROOT/out/arena/full"
mkdir -p "$FULL"

say() { printf '%s\n' "$*" | tee -a "$LOG"; }
# Run a command; keep its full output in $FULL/<name>.log, put the last N
# lines (and any error lines from earlier) in the short log.
run_trimmed() {
    name=$1; n=$2; shift 2
    full="$FULL/$name.log"
    "$@" >"$full" 2>&1
    rc=$?
    lines=$(wc -l <"$full" | tr -d ' ')
    if [ "$lines" -gt "$n" ]; then
        say "(output $lines lines; first error lines, then the last $n)"
        grep -n -m15 -E "error:|Error |\*\*\* |FAIL|No rule|not found|panic" "$full" |
            cut -c1-300 | tee -a "$LOG"
        say "..."
        tail -n "$n" "$full" | cut -c1-300 | tee -a "$LOG"
    else
        cut -c1-300 "$full" | tee -a "$LOG"
    fi
    return "$rc"
}

: >"$LOG"
say "== arena-run $(date '+%Y-%m-%d %H:%M:%S %Z') on $(hostname -s) ($(sysctl -n hw.model 2>/dev/null || uname -m))"
say "branch: $BRANCH  HEAD: $(git log -1 --format='%h %s')"
say "commits since $BASE:"
git log --oneline "$BASE..HEAD" 2>/dev/null | head -10 | sed 's/^/  /' | tee -a "$LOG"
say "files changed vs $BASE:"
git diff --stat "$BASE...HEAD" 2>/dev/null | tail -12 | sed 's/^/  /' | tee -a "$LOG"
avail=$(df -g "$ROOT" | awk 'NR==2 {print $4}')
say "free disk: ${avail} GiB$( [ "${avail:-0}" -lt 10 ] && echo '  (LOW: a world build may fail with ENOSPC)')"
say "tasks: $*"

case " $* " in
    *" kernel "*) ;;
    *)
        if git diff --name-only "$BASE...HEAD" -- Kernel/ 2>/dev/null | grep -q .; then
            say "WARNING: this branch changes Kernel/ but no 'kernel' task was requested;"
            say "         'boot' uses the previously built kernel."
        fi ;;
esac

status=0
for task in "$@"; do
    say ""
    say "==== task: $task ===="
    start=$(date +%s)
    case $task in
        loader)   run_trimmed loader 30 tools/bootloader/build.sh ;;
        kernel)   run_trimmed kernel 40 tools/darwin-gate.sh xnu ;;
        boot)     run_trimmed boot 120 tools/xnu-boot.sh ;;
        boot=*)   args=$(printf %s "${task#boot=}" | tr ',' ' ')
                  # shellcheck disable=SC2086
                  run_trimmed boot 120 tools/xnu-boot.sh $args ;;
        tests)    run_trimmed tests 40 tools/tests/run-tests.sh ;;
        selftest) run_trimmed selftest 40 tools/pipeline-selftest.sh --no-qemu ;;
        audit)    run_trimmed audit 40 python3 tools/macbuild-audit.py --severity high ;;
        stage=*)  run_trimmed "stage-${task#stage=}" 60 tools/macbuild.sh -q --target-arch x86_64 --stage "${task#stage=}" ;;
        world)    run_trimmed world 60 tools/macbuild.sh -q --audit --target-arch x86_64 ;;
        script=*) p=${task#script=}
                  [ -f "$p" ] || { say "no such script on this branch: $p"; status=2; break; }
                  run_trimmed "script-$(basename "$p")" 120 sh "$p" ;;
        *)        say "unknown task: $task"; status=2; break ;;
    esac
    rc=$?
    say "==== $task: exit $rc ($(( $(date +%s) - start ))s) ===="
    if [ "$rc" -ne 0 ]; then status=$rc; break; fi
done

say ""
say "== result: $( [ "$status" -eq 0 ] && echo OK || echo "FAILED (exit $status)" )"
say "full outputs: out/arena/full/   short log: ${LOG#$ROOT/}"
if command -v pbcopy >/dev/null 2>&1; then
    pbcopy <"$LOG" && echo "(short log copied to the clipboard)"
fi
rm -f "$0"
exit "$status"
