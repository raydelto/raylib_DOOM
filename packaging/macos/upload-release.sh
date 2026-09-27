#!/bin/sh
# Uploads every file in DIR to the draft GitHub Release for TAG,
# replacing assets of the same name.
#
#   packaging/macos/upload-release.sh TAG DIR
#
# Needs gh, with GH_TOKEN and GH_REPO set.
#
# The other OS release workflows run on the same tag and may create
# the draft at the same moment. Drafts are not tied to the tag, so a
# race can leave two; everyone uploads to the oldest, and a draft this
# job made that lost the race is deleted.
#
# A release the maintainer has already published is never changed:
# the script stops before touching anything, and checks again before
# each asset in case it was published meanwhile.

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 TAG DIR" >&2
    exit 2
fi

tag=$1
dir=$2

# "<id> <draft>" for each release of the tag, oldest first.
releases() {
    gh api --paginate "repos/$GH_REPO/releases" \
        --jq ".[] | select(.tag_name == \"$tag\") | \"\(.id) \(.draft)\"" |
        sort -n
}

refuse_published() {
    if printf '%s\n' "$1" | grep -q ' false$'; then
        echo "The release for $tag is already published; not changing it." >&2
        exit 1
    fi
}

list=$(releases)
refuse_published "$list"
mine=
if [ -z "$list" ]; then
    if url=$(gh release create "$tag" --draft --verify-tag \
               --title "$tag" --notes "raylib DOOM $tag"); then
        mine=$(gh api --paginate "repos/$GH_REPO/releases" \
                 --jq ".[] | select(.html_url == \"$url\") | .id")
    fi
    list=$(releases)
    refuse_published "$list"
fi

id=$(printf '%s\n' "$list" | awk 'NF { print $1; exit }')
[ -n "$id" ] || { echo "no draft release for $tag" >&2; exit 1; }
if [ -n "$mine" ] && [ "$mine" != "$id" ]; then
    gh api -X DELETE "repos/$GH_REPO/releases/$mine"
fi

echo "Uploading to draft release $id"
for f in "$dir"/*; do
    if [ "$(gh api "repos/$GH_REPO/releases/$id" --jq .draft)" != true ]; then
        echo "Release $id is no longer a draft; stopping." >&2
        exit 1
    fi
    name=$(basename "$f")
    old=$(gh api --paginate "repos/$GH_REPO/releases/$id/assets" \
            --jq ".[] | select(.name == \"$name\") | .id")
    [ -z "$old" ] || gh api -X DELETE "repos/$GH_REPO/releases/assets/$old"
    gh api -X POST -H "Content-Type: application/octet-stream" \
        "https://uploads.github.com/repos/$GH_REPO/releases/$id/assets?name=$name" \
        --input "$f" --jq .browser_download_url
done
