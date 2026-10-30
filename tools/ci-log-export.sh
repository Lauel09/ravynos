#!/bin/sh
# Publish a CI job's log to the commit as a comment.
#
# Why this exists: GitHub Actions run logs live on
# results-receiver.actions.githubusercontent.com and are fetched with a
# short-lived signed URL, so `gh run view --log` only works from a host that
# can reach that endpoint.  Some sanctioned environments (Arena sandboxes,
# restricted networks, CI-of-CI) can reach api.github.com but not the log
# blob host.  In that situation the *only* way a human or agent outside the
# runner can read a failure is if the runner writes the log somewhere plain
# api.github.com serves -- a commit comment does exactly that.
#
# This script is a last-mile courier, not a source of truth: the Actions run
# stays authoritative.  It is called from a `if: failure()` step with
# `continue-on-error: true`, so a courier failure never changes a job's
# verdict.
#
# Usage:
#   ci-log-export.sh --title TITLE [--log FILE]... [--dir DIR] [--note TEXT]
#                    [--max-bytes N] [--dry-run]
#
#   --title TITLE     heading; also the dedup key (a re-run of the same
#                     commit updates the same comment instead of adding one)
#   --log FILE        log to attach, repeatable.  Shell globs are fine: a
#                     pattern that matches nothing is reported as missing
#                     instead of aborting.
#   --dir DIR         also attach up to 6 most recently modified *.log files
#                     found under DIR (depth 4).  Jobs tee each step into
#                     $RUNNER_TEMP, so this needs no per-step bookkeeping.
#   --note TEXT       one-line context (which step failed, which host)
#   --max-bytes N     per-log truncation limit (default 52000, see below)
#   --dry-run         print the comment body instead of posting it
#
# Environment:
#   GITHUB_REPOSITORY, GITHUB_SHA, GITHUB_JOB, GITHUB_RUN_ID,
#   RUNNER_OS (all set by Actions; a local run needs at least the first two)
#   CI_LOG_NO_PUBLISH=1   behave like --dry-run (used by the unit tests)
#
# Exit status: 0 = posted/skipped cleanly, 1 = could not publish, 2 = usage.
# Every failure is non-fatal for the calling job.
#
# Size budget: GitHub caps a comment body at 65536 characters and 2 MB of
# API payload; 52000 leaves room for the heading, the per-log fences and the
# metadata block even when several logs are attached.
#
# Requires: gh (authenticated), git.  `contents: write` on the job.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck source=tools/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

TITLE=
SCAN_DIRS=
NOTES=
MAX_BYTES=52000
DRY_RUN=0
LOGS_LIST=$(mktemp "${TMPDIR:-/tmp}/ci-log-list.XXXXXX") || exit 1
BODY_FILE=$(mktemp "${TMPDIR:-/tmp}/ci-log-body.XXXXXX") || exit 1
trap 'rm -f "$LOGS_LIST" "$BODY_FILE" 2>/dev/null' EXIT INT TERM

while [ $# -gt 0 ]; do
    case "$1" in
        --title)     TITLE=${2:-}; shift 2 ;;
        --log)       printf '%s\n' "${2:-}" >> "$LOGS_LIST"; shift 2 ;;
        --dir)       SCAN_DIRS="$SCAN_DIRS${SCAN_DIRS:+
}${2:-}"; shift 2 ;;
        --note)      NOTES="$NOTES${NOTES:+
}${2:-}"; shift 2 ;;
        --max-bytes) MAX_BYTES=${2:-52000}; shift 2 ;;
        --dry-run)   DRY_RUN=1; shift ;;
        -h|--help)   sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           die "unknown option: $1 (try --help)" 2 ;;
    esac
done

[ -n "$TITLE" ] || die "--title is required" 2
[ -s "$LOGS_LIST" ] || [ -n "${SCAN_DIRS:-}" ] || die "at least one --log or --dir is required" 2
[ "${CI_LOG_NO_PUBLISH:-0}" = 1 ] && DRY_RUN=1

REPO=${GITHUB_REPOSITORY:-}
SHA=${GITHUB_SHA:-}
if [ -z "$REPO" ] || [ -z "$SHA" ]; then
    if have git; then
        [ -n "$SHA" ]  || SHA=$(git rev-parse HEAD 2>/dev/null || true)
        [ -n "$REPO" ] || REPO=$(git config --get remote.origin.url 2>/dev/null |
            sed -e 's#^.*github\.com[:/]##' -e 's#\.git$##')
    fi
fi

MARKER="<!-- ravynos-ci-log: $TITLE -->"

# Attach recent logs from the scanned directories.  Sorted by mtime and
# capped at six: a job that writes dozens of logs should not produce an
# unreadable blob.  mtime comes from the same BSD-then-GNU stat dance that
# tools/lib/common.sh uses for sizes.
if [ -n "$SCAN_DIRS" ]; then
    printf '%s\n' "$SCAN_DIRS" | while IFS= read -r d; do
        [ -n "$d" ] && [ -d "$d" ] || continue
        find "$d" -maxdepth 4 -type f -name '*.log' 2>/dev/null
    done | while IFS= read -r f; do
        [ -f "$f" ] || continue
        mt=$(stat -f%m "$f" 2>/dev/null || stat -c%Y "$f" 2>/dev/null || echo 0)
        printf '%s %s\n' "$mt" "$f"
    done | sort -n | tail -6 | while read -r _mt f; do
        grep -qxF "$f" "$LOGS_LIST" 2>/dev/null || printf '%s\n' "$f" >> "$LOGS_LIST"
    done
fi

# A job can fail before any step tees anything -- a broken setup action, a
# bad workflow file, a cancelled run.  Silence is the worst answer there, so
# post the metadata-only comment rather than refusing.
NO_LOGS=0
[ -s "$LOGS_LIST" ] || NO_LOGS=1

# --------------------------------------------------------------------------
# Build the comment body
# --------------------------------------------------------------------------
# Budget: GitHub refuses comment bodies over 65536 characters.  Split a
# 58000-character allowance across however many logs are attached, never
# exceeding the caller's per-log limit and never squeezing one below 3000.
LOG_COUNT=$(grep -c . "$LOGS_LIST" 2>/dev/null || echo 1)
[ "$LOG_COUNT" -ge 1 ] 2>/dev/null || LOG_COUNT=1
PER_LOG=$((58000 / LOG_COUNT))
[ "$PER_LOG" -gt "$MAX_BYTES" ] && PER_LOG=$MAX_BYTES
[ "$PER_LOG" -lt 3000 ] && PER_LOG=3000

{
    printf '%s\n' "$MARKER"
    printf '### CI failure log: %s\n\n' "$TITLE"
    printf 'Run `%s` job `%s` on `%s`%s%s, commit `%s`.\n' \
        "${GITHUB_RUN_ID:-local}" "${GITHUB_JOB:-?}" "${RUNNER_OS:-?}" \
        "${RUNNER_ARCH:+ ($RUNNER_ARCH)}" "" "${SHA}"
    printf '\n_The Actions run remains authoritative; this is a copy so the log\n'
    printf 'can be read through api.github.com without the log-blob host._\n'
    if [ -n "$NOTES" ]; then
        printf '\n'
        printf '%s\n' "$NOTES" | while IFS= read -r line; do printf '> %s\n' "$line"; done
    fi
    while IFS= read -r pattern; do
        [ -n "$pattern" ] || continue
        matched=0
        # An exact path that exists wins; otherwise let the shell expand the
        # pattern (an unmatched pattern stays literal and is reported missing).
        if [ -f "$pattern" ]; then
            set -- "$pattern"
        else
            set -- $pattern
        fi
        for f in "$@"; do
            [ -f "$f" ] || continue
            matched=1
            name=${f#"${RUNNER_TEMP:-/nonexistent}/"}
            [ "$name" = "$f" ] && name=$(basename "$f")
            size=$(wc -c < "$f" | tr -d ' ')
            printf '\n<details%s><summary><code>%s</code> (%s bytes)%s</summary>\n\n```\n' \
                " open" "$name" "$size" ""
            if [ "$size" -gt "$PER_LOG" ]; then
                printf '[... %s leading bytes elided ...]\n' "$((size - PER_LOG))"
                # tail -c can start mid-line; drop that partial line.
                # The first line of a byte-tail is usually a fragment, so
                # drop it -- but never drop the only line (a log that is one
                # enormous line would otherwise vanish entirely).
                tail -c "$PER_LOG" "$f" |
                    awk 'NR==1{first=$0; next} {print} END{if (NR==1) print first}'
            else
                cat "$f"
            fi
            printf '```\n\n</details>\n'
        done
        [ "$matched" = 1 ] || printf '\n_log not produced: `%s` (step failed before writing it)_\n' "$pattern"
    done < "$LOGS_LIST"
    if [ "$NO_LOGS" = 1 ]; then
        printf '\n**No log files were produced.** This job most likely failed '
        printf 'before any step could tee output (environment setup, a broken '
        printf 'workflow or action definition, or cancellation).\n'
    fi
    printf '\n---\n_Host: `%s`%s. Exported by `tools/ci-log-export.sh`._\n' \
        "$(uname -srm 2>/dev/null || echo unknown)" \
        "${GITHUB_WORKFLOW:+, workflow \`$GITHUB_WORKFLOW\`}"
} > "$BODY_FILE" || die "could not assemble the comment body" 1

BODY_BYTES=$(wc -c < "$BODY_FILE" | tr -d ' ')
if [ "$BODY_BYTES" -gt 62000 ]; then
    # Pathological case (many logs, weird line lengths): keep the tail, which
    # is where the failure usually is, and say so.
    warn "comment body is $BODY_BYTES bytes; truncating to 62000"
    tail -c 61800 "$BODY_FILE" > "$BODY_FILE.cut" &&
        { printf '\n_(earlier output truncated by ci-log-export.sh)_\n' >> "$BODY_FILE.cut" &&
          mv "$BODY_FILE.cut" "$BODY_FILE"; } ||
        rm -f "$BODY_FILE.cut"
    BODY_BYTES=$(wc -c < "$BODY_FILE" | tr -d ' ')
fi
log "  comment body: $BODY_BYTES bytes -> $TITLE"

if [ "$DRY_RUN" = 1 ]; then
    cat "$BODY_FILE"
    exit 0
fi

# --------------------------------------------------------------------------
# Publish: update the existing comment for this title, else create one.
# --------------------------------------------------------------------------
if ! have gh; then
    warn "gh is not installed; cannot publish the log (run is still on GitHub)"
    exit 1
fi

# Find an existing comment with this exact marker (a re-run of the same job
# on the same commit).  Matching is done in Python to avoid a JSON parser
# dependency; gh output is only used as data, never evaluated.
existing=
if have python3; then
    existing=$(gh api --paginate "repos/$REPO/commits/$SHA/comments?per_page=100" 2>/dev/null |
        python3 -c '
import json, sys
marker = sys.argv[1]
try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)
if isinstance(data, dict):
    data = [data]
for c in data if isinstance(data, list) else []:
    if marker in (c.get("body") or ""):
        print(c.get("id"))
        break
' "$MARKER" 2>/dev/null)
fi

if [ -n "$existing" ]; then
    url=$(gh api --method PATCH "repos/$REPO/commits/$SHA/comments/$existing" \
        -F "body=@$BODY_FILE" --jq .html_url 2>/dev/null) || url=
    if [ -n "$url" ]; then
        log "  updated comment: $url"
        exit 0
    fi
    warn "could not update comment $existing; creating a new one"
fi

url=$(gh api --method POST "repos/$REPO/commits/$SHA/comments" \
    -F "body=@$BODY_FILE" --jq .html_url 2>/dev/null) || url=
if [ -n "$url" ]; then
    log "  posted comment: $url"
    exit 0
fi

warn "could not publish the log comment to $REPO@$SHA"
warn "(needs contents: write; on fork PRs the token is read-only)"
exit 1
