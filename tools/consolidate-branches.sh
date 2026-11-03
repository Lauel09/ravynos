#!/usr/bin/env bash
#
# consolidate-branches.sh - fold agent-created branches into the integration
# branch of both ravynos repos, then delete the folded branches.
#
# Defaults to a dry run.  A branch is deleted only when its local and remote
# tips are already ancestors of the target branch or merge cleanly.  If the
# local and remote tips have diverged, the branch is kept.  Conflicting merges
# are aborted, the branch is kept, and the script continues with the next one.
#
# Branches are selected by glob.  The defaults cover the names produced by the
# arena/qwen agents: arena/*, qwen/*, qwen-*, continue-ravynos-roadmap-*.
#
# Usage:
#   tools/consolidate-branches.sh                  # dry run, both repos
#   tools/consolidate-branches.sh --apply          # merge + delete locally
#   tools/consolidate-branches.sh --apply --push   # also push + delete remotely
#
# Options:
#   --apply                  perform merges and local branch deletion
#   --push                   push the target and delete remote branches
#                            (implies --apply); without it remote branches
#                            are left in place
#   --on-conflict=abort|ours|theirs
#                            conflict policy (default: abort)
#   --pattern GLOB           add a branch glob (repeatable)
#   --slim PATH              path to ravynos-slim (default: sibling directory)
#   --full PATH              path to ravynos (default: sibling directory)
#   --slim-target BRANCH     default: main
#   --full-target BRANCH     default: fork/darwin-dev
#   --slim-remote NAME       default: origin
#   --full-remote NAME       default: fork
#   --no-fetch               do not fetch/prune before planning
#   -h, --help               show this help
#
# The script never force-pushes.  Run it from either repo's tools/ directory
# as long as ravynos and ravynos-slim are siblings.

set -euo pipefail

APPLY=0
PUSH=0
FETCH=1
ON_CONFLICT=abort
PATTERNS=("arena/*" "qwen/*" "qwen-*" "continue-ravynos-roadmap-*")

SLIM_TARGET=main
FULL_TARGET=fork/darwin-dev
SLIM_REMOTE=origin
FULL_REMOTE=fork

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORKSPACE=$(cd "$SCRIPT_DIR/../.." 2>/dev/null && pwd || true)
SLIM=${SLIM:-${WORKSPACE:+$WORKSPACE/ravynos-slim}}
FULL=${FULL:-${WORKSPACE:+$WORKSPACE/ravynos}}

usage() {
  sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
  case "$1" in
    --apply) APPLY=1 ;;
    --push) PUSH=1; APPLY=1 ;;
    --no-fetch) FETCH=0 ;;
    --on-conflict=*) ON_CONFLICT=${1#*=} ;;
    --pattern) shift; PATTERNS+=("$1") ;;
    --slim) shift; SLIM=$1 ;;
    --full) shift; FULL=$1 ;;
    --slim-target) shift; SLIM_TARGET=$1 ;;
    --full-target) shift; FULL_TARGET=$1 ;;
    --slim-remote) shift; SLIM_REMOTE=$1 ;;
    --full-remote) shift; FULL_REMOTE=$1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

case "$ON_CONFLICT" in
  abort|ours|theirs) ;;
  *) echo "invalid --on-conflict value: $ON_CONFLICT" >&2; exit 2 ;;
esac

matches_pattern() {
  local name=$1 pat
  for pat in "${PATTERNS[@]}"; do
    case "$name" in
      $pat) return 0 ;;
    esac
  done
  return 1
}

has_ref() {
  git -C "$1" show-ref --verify --quiet "$2"
}

delete_plan() {
  local repo=$1 remote=$2 branch=$3
  local local_ref="refs/heads/$branch"
  local remote_ref="refs/remotes/$remote/$branch"
  local have_local=0 have_remote=0

  if has_ref "$repo" "$local_ref"; then have_local=1; fi
  if has_ref "$repo" "$remote_ref"; then have_remote=1; fi

  if [ "$have_local" -eq 1 ] && [ "$have_remote" -eq 1 ]; then
    if [ "$PUSH" -eq 1 ]; then
      echo "delete local and remote $remote/$branch"
    else
      echo "delete local; remote $remote/$branch kept (use --push)"
    fi
  elif [ "$have_local" -eq 1 ]; then
    echo "delete local"
  elif [ "$have_remote" -eq 1 ]; then
    if [ "$PUSH" -eq 1 ]; then
      echo "delete remote $remote/$branch"
    else
      echo "remote $remote/$branch kept (use --push)"
    fi
  else
    echo "nothing to delete"
  fi
}

repo_branches() {
  local repo=$1 remote=$2
  {
    git -C "$repo" for-each-ref --format='%(refname:short)' "refs/remotes/$remote" 2>/dev/null \
      | sed "s#^$remote/##"
    git -C "$repo" for-each-ref --format='%(refname:short)' refs/heads 2>/dev/null
  } | grep -v '^HEAD$' | sort -u
}

consolidate() {
  local label=$1 repo=$2 remote=$3 target=$4
  local branch merge_ref current tmp wt names
  local local_ref remote_ref have_local have_remote all_merged
  local merged=() kept=() remaining=

  echo
  echo "=== $label: $repo (target $target) ==="

  if ! git -C "$repo" rev-parse --git-dir >/dev/null 2>&1; then
    echo "skip: not a git repository: $repo" >&2
    return 0
  fi
  if ! has_ref "$repo" "refs/heads/$target"; then
    echo "skip: target branch not found: $target" >&2
    return 0
  fi

  if [ "$FETCH" -eq 1 ]; then
    echo "fetching $remote..."
    git -C "$repo" fetch --prune "$remote"
  fi

  current=$(git -C "$repo" symbolic-ref --short HEAD 2>/dev/null || true)
  wt=
  tmp=
  if [ "$APPLY" -eq 1 ]; then
    if [ "$current" = "$target" ]; then
      if [ -n "$(git -C "$repo" status --porcelain)" ]; then
        echo "skip: $repo has uncommitted changes on $target" >&2
        return 1
      fi
      wt=$repo
    else
      if git -C "$repo" worktree list --porcelain | grep -qx "branch refs/heads/$target"; then
        echo "skip: $target is checked out in another worktree" >&2
        return 1
      fi
      tmp=$(mktemp -d "${TMPDIR:-/tmp}/consolidate.XXXXXX")
      git -C "$repo" worktree add "$tmp" "$target" >/dev/null
      wt=$tmp
    fi
  fi

  cleanup_worktree() {
    if [ -n "$tmp" ]; then
      git -C "$repo" worktree remove --force "$tmp" >/dev/null 2>&1 || true
      rm -rf "$tmp"
      tmp=
    fi
  }
  trap cleanup_worktree EXIT

  names=$(repo_branches "$repo" "$remote")

  while IFS= read -r branch; do
    [ -n "$branch" ] || continue
    matches_pattern "$branch" || continue
    [ "$branch" = "$target" ] && continue

    local_ref="refs/heads/$branch"
    remote_ref="refs/remotes/$remote/$branch"
    have_local=0
    have_remote=0
    if has_ref "$repo" "$local_ref"; then have_local=1; fi
    if has_ref "$repo" "$remote_ref"; then have_remote=1; fi

    if [ "$have_local" -eq 0 ] && [ "$have_remote" -eq 0 ]; then
      continue
    fi

    all_merged=1
    if [ "$have_local" -eq 1 ] &&
       ! git -C "$repo" merge-base --is-ancestor "$local_ref" "$target"; then
      all_merged=0
    fi
    if [ "$have_remote" -eq 1 ] &&
       ! git -C "$repo" merge-base --is-ancestor "$remote_ref" "$target"; then
      all_merged=0
    fi

    if [ "$all_merged" -eq 1 ]; then
      echo "  $branch: already merged -> $(delete_plan "$repo" "$remote" "$branch")"
      merged+=("$branch")
      continue
    fi

    merge_ref=
    if [ "$have_local" -eq 1 ] && [ "$have_remote" -eq 1 ]; then
      if git -C "$repo" merge-base --is-ancestor "$local_ref" "$remote_ref"; then
        merge_ref=$remote_ref
      elif git -C "$repo" merge-base --is-ancestor "$remote_ref" "$local_ref"; then
        merge_ref=$local_ref
      else
        echo "  $branch: local and $remote/$branch have diverged - branch kept" >&2
        kept+=("$branch")
        continue
      fi
    elif [ "$have_local" -eq 1 ]; then
      merge_ref=$local_ref
    else
      merge_ref=$remote_ref
    fi

    if [ "$APPLY" -eq 0 ]; then
      echo "  $branch: would merge into $target, then $(delete_plan "$repo" "$remote" "$branch")"
      continue
    fi

    echo "  $branch: merging into $target..."
    if [ "$ON_CONFLICT" = ours ]; then
      git -C "$wt" merge --no-ff --no-edit -X ours "$merge_ref"
    elif [ "$ON_CONFLICT" = theirs ]; then
      git -C "$wt" merge --no-ff --no-edit -X theirs "$merge_ref"
    else
      git -C "$wt" merge --no-ff --no-edit "$merge_ref"
    fi && {
      echo "  $branch: merged"
      merged+=("$branch")
    } || {
      echo "  $branch: conflict - merge aborted, branch kept" >&2
      git -C "$wt" merge --abort 2>/dev/null || true
      kept+=("$branch")
    }
  done <<< "$names"

  if [ "$APPLY" -eq 0 ]; then
    cleanup_worktree
    trap - EXIT
    return 0
  fi

  cleanup_worktree
  trap - EXIT

  if [ "$PUSH" -eq 1 ]; then
    echo "pushing $target to $remote..."
    git -C "$repo" push "$remote" "refs/heads/$target:refs/heads/$target"
  fi

  for branch in "${merged[@]:-}"; do
    [ -n "$branch" ] || continue
    if [ "$PUSH" -eq 1 ] && has_ref "$repo" "refs/remotes/$remote/$branch"; then
      echo "  deleting remote $remote/$branch..."
      git -C "$repo" push "$remote" --delete "refs/heads/$branch"
    fi
    if has_ref "$repo" "refs/heads/$branch"; then
      echo "  deleting local $branch..."
      git -C "$repo" branch -D "$branch" >/dev/null 2>&1 \
        || echo "    could not delete local $branch (checked out elsewhere?)" >&2
    fi
  done

  if [ "$PUSH" -eq 0 ]; then
    remaining=
    for branch in "${merged[@]:-}"; do
      [ -n "$branch" ] || continue
      if has_ref "$repo" "refs/remotes/$remote/$branch"; then
        remaining="${remaining}${remaining:+, }$branch"
      fi
    done
    if [ -n "$remaining" ]; then
      echo "remote branches kept (rerun with --push to delete): $remaining" >&2
    fi
  fi

  if [ "$PUSH" -eq 1 ]; then
    git -C "$repo" fetch --prune "$remote" >/dev/null 2>&1 || true
  fi

  if [ ${#kept[@]} -gt 0 ]; then
    echo "kept branches (conflict or divergence): ${kept[*]}" >&2
  fi
  return 0
}

if [ -z "$SLIM" ] || [ ! -d "$SLIM" ]; then
  echo "slim repo not found (use --slim PATH)" >&2
else
  consolidate slim "$SLIM" "$SLIM_REMOTE" "$SLIM_TARGET"
fi

if [ -z "$FULL" ] || [ ! -d "$FULL" ]; then
  echo "full repo not found (use --full PATH)" >&2
else
  consolidate full "$FULL" "$FULL_REMOTE" "$FULL_TARGET"
fi
