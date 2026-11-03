#!/bin/bash
# Installs the ID5 pre-push guard as a real file in the shared hooks dir (all worktrees use it).
# Re-run after editing id5/tools/git-hooks/pre-push. Also creates an empty local denylist if none exists.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
common="$(git -C "$here" rev-parse --git-common-dir)"
case "$common" in /*) ;; *) common="$here/$common" ;; esac
hooks="$common/hooks"
[ -L "$hooks/pre-push" ] && rm "$hooks/pre-push"            # old symlink install
install -m 0755 "$here/pre-push" "$hooks/pre-push"
deny="$common/info/push-denylist"
[ -e "$deny" ] || printf '# One identifier per line (case-insensitive, fixed string). Local only, never committed.\n' > "$deny"
echo "installed $hooks/pre-push; denylist: $deny ($(grep -vc '^#' "$deny" || true) entries)"
