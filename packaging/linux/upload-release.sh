#!/bin/sh
# Uploads every file in DIST_DIR to the draft GitHub Release for $TAG,
# creating the draft if there is none.
#
#   GH_REPO=owner/repo TAG=v1.2.3 packaging/linux/upload-release.sh DIST_DIR
#
# Only runs for a tag push (GITHUB_EVENT_NAME=push, GITHUB_REF_TYPE=tag):
# a workflow_dispatch run, even on a tag, keeps its packages as an
# artifact. Never touches a published release.
#
# The other OS release workflows run on the same tag and may create the
# draft at the same moment. Drafts are not tied to the tag, so a race
# can leave two; everyone uploads to the oldest, and a draft this job
# made that lost the race is deleted.

set -eu

dist=$1
: "${GH_REPO:?}" "${TAG:?}"

if [ "${GITHUB_EVENT_NAME:-}" != push ] || [ "${GITHUB_REF_TYPE:-}" != tag ]; then
    echo "not a tag push (event ${GITHUB_EVENT_NAME:-unset}," \
         "ref type ${GITHUB_REF_TYPE:-unset}): not releasing" >&2
    exit 1
fi

# "<id> <draft>" for each release of the tag.
releases() {
    gh api --paginate "repos/$GH_REPO/releases" \
        --jq ".[] | select(.tag_name == \"$TAG\") | \"\(.id) \(.draft)\""
}
drafts() {
    releases | awk '$2 == "true" { print $1 }' | sort -n
}
check_unpublished() {
    published=$(releases | awk '$2 == "false" { print $1 }')
    if [ -n "$published" ]; then
        echo "$TAG already has a published release ($published):" \
             "not changing it" >&2
        exit 1
    fi
}

check_unpublished

mine=
if [ -z "$(drafts)" ]; then
    if url=$(gh release create "$TAG" --draft --verify-tag \
               --title "$TAG" --notes "raylib DOOM $TAG"); then
        mine=$(gh api --paginate "repos/$GH_REPO/releases" \
                 --jq ".[] | select(.html_url == \"$url\") | .id")
    fi
fi

check_unpublished
id=$(drafts | head -1)
[ -n "$id" ] || { echo "no draft release for $TAG" >&2; exit 1; }
if [ -n "$mine" ] && [ "$mine" != "$id" ]; then
    echo "another job created draft $id first; deleting draft $mine"
    gh api -X DELETE "repos/$GH_REPO/releases/$mine"
fi

for f in "$dist"/*; do
    # Checked before every change, in case a maintainer publishes the
    # release while this runs.
    draft=$(gh api "repos/$GH_REPO/releases/$id" --jq .draft)
    if [ "$draft" != true ]; then
        echo "release $id is no longer a draft: stopping" >&2
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
