#!/usr/bin/env bash
set -euo pipefail

# Fetches external dependencies for this repository:
# - Stephan Mueller jitterentropy-library
# - NIST SP800-90B EntropyAssessment
#
# Places them into:
# - external/jitterentropy
# - cpujitter-qualifier/external/SP800-90B_EntropyAssessment

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JENT_DIR="$ROOT_DIR/external/jitterentropy"
NIST_DIR="$ROOT_DIR/cpujitter-qualifier/external/SP800-90B_EntropyAssessment"

JENT_REPO_DEFAULT="https://github.com/smuellerDD/jitterentropy-library.git"
NIST_REPO_DEFAULT="https://github.com/usnistgov/SP800-90B_EntropyAssessment.git"

JENT_REPO="${JENT_REPO:-$JENT_REPO_DEFAULT}"
NIST_REPO="${NIST_REPO:-$NIST_REPO_DEFAULT}"
JENT_REF="${JENT_REF:-master}"
NIST_REF="${NIST_REF:-master}"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--jent-repo URL] [--jent-ref REF] [--nist-repo URL] [--nist-ref REF] [--no-update]

Environment overrides:
  JENT_REPO, JENT_REF, NIST_REPO, NIST_REF

Options:
  --jent-repo URL   Override jitterentropy repository URL
  --jent-ref REF    Branch/tag/commit to checkout for jitterentropy (default: master)
  --nist-repo URL   Override NIST repository URL
  --nist-ref REF    Branch/tag/commit to checkout for NIST (default: master)
  --no-update       Do not pull/fetch if target directory already exists
  -h, --help        Show this help
USAGE
}

NO_UPDATE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --jent-repo) JENT_REPO="$2"; shift 2 ;;
    --jent-ref) JENT_REF="$2"; shift 2 ;;
    --nist-repo) NIST_REPO="$2"; shift 2 ;;
    --nist-ref) NIST_REF="$2"; shift 2 ;;
    --no-update) NO_UPDATE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

clone_or_update_git() {
  local repo_url="$1"
  local ref="$2"
  local target_dir="$3"

  if [[ -d "$target_dir/.git" ]]; then
    echo "[deps] Existing git repo found at: $target_dir"
    if [[ "$NO_UPDATE" -eq 1 ]]; then
      echo "[deps] --no-update set; skipping update for $target_dir"
      return 0
    fi
    git -C "$target_dir" fetch --all --tags
    git -C "$target_dir" checkout "$ref"
    git -C "$target_dir" pull --ff-only || true
  else
    if [[ -e "$target_dir" && ! -d "$target_dir/.git" ]]; then
      echo "[deps] Removing non-git directory at $target_dir"
      rm -rf "$target_dir"
    fi
    echo "[deps] Cloning $repo_url -> $target_dir"
    git clone "$repo_url" "$target_dir"
    git -C "$target_dir" checkout "$ref"
  fi
}

mkdir -p "$(dirname "$JENT_DIR")"
mkdir -p "$(dirname "$NIST_DIR")"

clone_or_update_git "$JENT_REPO" "$JENT_REF" "$JENT_DIR"
clone_or_update_git "$NIST_REPO" "$NIST_REF" "$NIST_DIR"

echo "[deps] Done. Installed/updated:"
echo "  - $JENT_DIR"
echo "  - $NIST_DIR"
