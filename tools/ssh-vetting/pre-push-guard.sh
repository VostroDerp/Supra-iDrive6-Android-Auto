#!/bin/bash
# Pre-push guard: refuse to push session artifacts / binaries / proprietary
# files to GitHub. Files already present in upstream/main are allowed
# (upstream itself ships e.g. rhmiapp zips — mirror syncs must pass).
# Installed as .git/hooks/pre-push via symlink.

FORBIDDEN='FUTURE\.md|pulled/|captures/|recon[0-9]|nbt-unlock-|mgrecon|\.so($|\.)|\.zip$|\.bin$|\.7z$|\.iso$|\.log$|\.pcap$'
MAX_KB=200
fail=0

while read local_ref local_sha remote_ref remote_sha; do
  [ "$local_sha" = "0000000000000000000000000000000000000000" ] && continue
  if [ "$remote_sha" = "0000000000000000000000000000000000000000" ]; then
    base=$(git merge-base upstream/main "$local_sha" 2>/dev/null || git merge-base main "$local_sha" 2>/dev/null)
    [ -z "$base" ] && continue
    range="$base..$local_sha"
  else
    range="$remote_sha..$local_sha"
  fi

  files=$(git diff --name-only "$range" 2>/dev/null)
  [ -z "$files" ] && continue

  while IFS= read -r f; do
    # skip files already in upstream — allowed by definition
    git cat-file -e "upstream/main:$f" 2>/dev/null && continue
    if echo "$f" | grep -qE "$FORBIDDEN"; then
      echo "PUSH BLOCKED — forbidden artifact: $f" >&2; fail=1; continue
    fi
    if git cat-file -e "$local_sha:$f" 2>/dev/null; then
      sz=$(git cat-file -s "$local_sha:$f")
      if [ "$sz" -gt $((MAX_KB*1024)) ]; then
        echo "PUSH BLOCKED — $f is $((sz/1024))KB (> ${MAX_KB}KB) and not in upstream" >&2; fail=1
      fi
    fi
  done <<< "$files"
done

[ "$fail" = 1 ] && echo "Clean it up or commit on a local-only branch. See tools/ssh-vetting/pre-push-guard.sh" >&2
exit $fail
