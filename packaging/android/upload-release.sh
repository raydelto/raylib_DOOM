#!/bin/sh
# Uploads the APK and its .sha256 in DIST_DIR to the draft GitHub
# Release for $TAG (with packaging/linux/upload-release.sh, which
# makes the draft if no other job has yet), then adds the Android
# section, RELEASE-NOTES.md, to the draft's notes once.
#
#   GH_REPO=owner/repo TAG=v1.2.3 packaging/android/upload-release.sh DIST_DIR
#
# Like the Linux script, only for a tag push, and never touches a
# published release.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
: "${GH_REPO:?}" "${TAG:?}"

sh "$here/../linux/upload-release.sh" "$1"

marker='<!-- raylibdoom-android -->'
id=$(gh api --paginate "repos/$GH_REPO/releases" \
       --jq ".[] | select(.tag_name == \"$TAG\" and .draft) | .id" | sort -n | head -1)
[ -n "$id" ] || { echo "no draft release for $TAG" >&2; exit 1; }
body=$(gh api "repos/$GH_REPO/releases/$id" --jq '.body // ""')
case $body in *"$marker"*) echo "release notes already have the Android section"; exit 0 ;; esac

version=${TAG#v}
notes=$(sed "s/@VERSION@/$version/g" "$here/RELEASE-NOTES.md")
new=$(printf '%s\n\n%s\n%s\n' "$body" "$marker" "$notes")
# Checked again right before the change, in case it was just published.
[ "$(gh api "repos/$GH_REPO/releases/$id" --jq .draft)" = true ] ||
    { echo "release $id is no longer a draft: not changing its notes" >&2; exit 1; }
gh api -X PATCH "repos/$GH_REPO/releases/$id" -f body="$new" --jq .html_url
