#!/bin/sh
# Publish this fork to the authenticated GitHub account.
#
# Prerequisite: run `gh auth login` first. This script never stores
# credentials; it relies on the gh CLI's own authentication.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

command -v gh >/dev/null 2>&1 || { echo "publish: gh is not installed" >&2; exit 2; }
gh auth status >/dev/null 2>&1 || { echo "publish: run 'gh auth login' first" >&2; exit 2; }

BRANCH=fork/darwin-dev
BASELINE_TAG=baseline-60aa4ff4b

# Create the fork if it does not exist yet (idempotent).
if ! gh repo view --json nameWithOwner >/dev/null 2>&1; then
    gh repo fork ravynsoft/ravynos --clone=false
fi

FORK=$(gh repo view --json nameWithOwner -q .nameWithOwner)
echo "publish: fork is $FORK"

# Point 'fork' at it.
if git remote get-url fork >/dev/null 2>&1; then
    git remote set-url fork "https://github.com/${FORK}.git"
else
    git remote add fork "https://github.com/${FORK}.git"
fi

git push -u fork "$BRANCH"
git push fork "$BASELINE_TAG" 2>/dev/null || true

# Make the fork's presentation match its intent.
gh repo edit "$FORK" \
    --description "macOS-first fork of ravynsoft/ravynos: builds on Apple Silicon, ships a portable root-free source->ISO->boot-verdict pipeline, and improves OS fidelity with need-driven reverse engineering." \
    --homepage "https://github.com/${FORK}" 2>/dev/null || true

echo "publish: done -> https://github.com/${FORK}/tree/${BRANCH}"
echo "publish: open a PR or set the default branch in the GitHub UI if desired."