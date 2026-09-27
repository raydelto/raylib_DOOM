#!/bin/sh
# Uploads files to the draft GitHub Release for a tag, creating the
# draft if there is none. Refuses to touch a release that has been
# published: maintainers publish, and re-running a tag job afterwards
# must not replace public assets.
#
#   GH_REPO=owner/repo packaging/windows/upload-to-draft.sh TAG FILE...
#
# The other OS release workflows run on the same tag and may create
# the draft at the same moment. Drafts are not tied to the tag, so a
# race can leave two; everyone uploads to the oldest, and a draft this
# script made that lost the race is deleted.

set -eu

if [ $# -lt 2 ] || [ -z "${GH_REPO:-}" ]; then
    echo "usage: GH_REPO=owner/repo $0 TAG FILE..." >&2
    exit 2
fi
tag=$1
shift

# "<id> <draft>" for every release of the tag, oldest first. A failed
# listing stops the script instead of looking like "no releases".
releases() {
    list=$(gh api --paginate "repos/$GH_REPO/releases" \
        --jq ".[] | select(.tag_name == \"$tag\") | \"\(.id) \(.draft)\"") ||
        { echo "could not list releases" >&2; exit 1; }
    [ -z "$list" ] || printf '%s\n' "$list" | sort -n
}

refuse_published() {
    published=$(printf '%s\n' "$1" | awk '$2 == "false" { print $1 }')
    if [ -n "$published" ]; then
        echo "release $published for $tag is published; not changing it" >&2
        exit 1
    fi
}

all=$(releases) || exit 1
refuse_published "$all"

mine=
if [ -z "$all" ]; then
    if url=$(gh release create "$tag" --draft --verify-tag \
               --title "$tag" --notes "raylib DOOM $tag"); then
        mine=$(gh api "repos/$GH_REPO/releases" \
                 --jq ".[] | select(.html_url == \"$url\") | .id")
    fi
    all=$(releases) || exit 1
    refuse_published "$all"
fi

id=$(printf '%s\n' "$all" | awk '$2 == "true" { print $1; exit }')
[ -n "$id" ] || { echo "no draft release for $tag" >&2; exit 1; }
if [ -n "$mine" ] && [ "$mine" != "$id" ]; then
    gh api -X DELETE "repos/$GH_REPO/releases/$mine"
fi

for f in "$@"; do
    # Check again before every change, in case the draft was
    # published while this job ran.
    draft=$(gh api "repos/$GH_REPO/releases/$id" --jq .draft)
    if [ "$draft" != true ]; then
        echo "release $id for $tag is no longer a draft; stopping" >&2
        exit 1
    fi
    name=$(basename "$f")
    old=$(gh api "repos/$GH_REPO/releases/$id/assets" --paginate \
            --jq ".[] | select(.name == \"$name\") | .id")
    [ -z "$old" ] || gh api -X DELETE "repos/$GH_REPO/releases/assets/$old"
    gh api -X POST -H "Content-Type: application/octet-stream" \
        "https://uploads.github.com/repos/$GH_REPO/releases/$id/assets?name=$name" \
        --input "$f" --jq .browser_download_url
done
