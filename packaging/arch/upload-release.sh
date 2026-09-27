#!/bin/sh
# Uploads files to the draft GitHub Release for a tag. Never touches a
# published release and never creates a tag: maintainers publish.
#
#   GH_REPO=owner/repo GITHUB_EVENT_NAME=... GITHUB_REF_TYPE=... \
#       packaging/arch/upload-release.sh TAG FILE...
#
# A v* tag push (GITHUB_EVENT_NAME=push, GITHUB_REF_TYPE=tag) creates
# the draft if there is none. The other OS release workflows run on the
# same tag and may create it at the same moment; drafts are not tied to
# the tag, so a race can leave two. Everyone uploads to the oldest, and
# a draft this script made that lost the race is deleted.
#
# workflow_dispatch with a tag adds the package to that tag's existing
# draft, for a draft made before this workflow existed. It never
# creates a release: no draft, or a published one, is an error.
#
# Any other event does nothing and fails.

set -eu

if [ $# -lt 2 ] || [ -z "${GH_REPO:-}" ]; then
    echo "usage: GH_REPO=owner/repo $0 TAG FILE..." >&2
    exit 2
fi
tag=$1
shift

event=${GITHUB_EVENT_NAME:-unset}
if [ "$event" = push ] && [ "${GITHUB_REF_TYPE:-}" = tag ]; then
    may_create=1
elif [ "$event" = workflow_dispatch ]; then
    may_create=
else
    echo "event $event (ref type ${GITHUB_REF_TYPE:-unset}): not releasing" >&2
    exit 1
fi

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
    if [ -z "$may_create" ]; then
        echo "$tag has no draft release; workflow_dispatch only adds to an" \
             "existing draft" >&2
        exit 1
    fi
    if url=$(gh release create "$tag" --draft --verify-tag \
               --title "$tag" --notes "raylib DOOM $tag"); then
        mine=$(gh api --paginate "repos/$GH_REPO/releases" \
                 --jq ".[] | select(.html_url == \"$url\") | .id")
    fi
    all=$(releases) || exit 1
    refuse_published "$all"
fi

id=$(printf '%s\n' "$all" | awk '$2 == "true" { print $1; exit }')
[ -n "$id" ] || { echo "no draft release for $tag" >&2; exit 1; }
if [ -n "$mine" ] && [ "$mine" != "$id" ]; then
    # Ours, but only deleted while it is still a draft; if someone
    # published it meanwhile, it is the maintainer's to clean up.
    if [ "$(gh api "repos/$GH_REPO/releases/$mine" --jq .draft)" = true ]; then
        echo "another job created draft $id first; deleting draft $mine"
        gh api -X DELETE "repos/$GH_REPO/releases/$mine"
    else
        echo "release $mine that this job created is no longer a draft; leaving it" >&2
    fi
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
    old=$(gh api --paginate "repos/$GH_REPO/releases/$id/assets" \
            --jq ".[] | select(.name == \"$name\") | .id")
    [ -z "$old" ] || gh api -X DELETE "repos/$GH_REPO/releases/assets/$old"
    gh api -X POST -H "Content-Type: application/octet-stream" \
        "https://uploads.github.com/repos/$GH_REPO/releases/$id/assets?name=$name" \
        --input "$f" --jq .browser_download_url
done
