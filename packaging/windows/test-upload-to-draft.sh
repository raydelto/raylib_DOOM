#!/bin/sh
# Tests upload-to-draft.sh against a fake gh that serves releases from
# a JSON file and records every call. Nothing talks to GitHub.
# Needs jq.
#
#   packaging/windows/test-upload-to-draft.sh

set -eu

here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir "$work/bin"
cat > "$work/bin/gh" <<'EOF'
#!/bin/sh
# Fake gh: releases come from $MOCK/releases.json, calls go to $MOCK/calls.
echo "gh $*" >> "$MOCK/calls"
if [ "$1" = release ] && [ "$2" = create ]; then
    id=$(jq '[.[].id] | max + 1' "$MOCK/releases.json")
    url="https://github.com/o/r/releases/tag/untagged-$id"
    jq --arg tag "$3" --argjson id "$id" --arg url "$url" \
        '. + [{id: $id, tag_name: $tag, draft: true, html_url: $url, assets: []}]' \
        "$MOCK/releases.json" > "$MOCK/new.json"
    mv "$MOCK/new.json" "$MOCK/releases.json"
    echo "$id" > "$MOCK/created"
    # Another OS job created an older draft at the same moment.
    if [ -e "$MOCK/race" ]; then
        jq --arg tag "$3" \
            '. + [{id: 1, tag_name: $tag, draft: true, html_url: "u1", assets: []}]' \
            "$MOCK/releases.json" > "$MOCK/new.json"
        mv "$MOCK/new.json" "$MOCK/releases.json"
    fi
    echo "$url"
    exit 0
fi
[ ! -e "$MOCK/api-fails" ] || { echo "HTTP 502" >&2; exit 1; }
shift    # api
method=GET path= filter=.
while [ $# -gt 0 ]; do
    case $1 in
        -X) method=$2; shift ;;
        --jq) filter=$2; shift ;;
        -H|--input) shift ;;
        -*) ;;
        *) path=$1 ;;
    esac
    shift
done
case $method in
    POST) echo "https://example.invalid/asset"; exit 0 ;;
    DELETE) exit 0 ;;
esac
case $path in
    */releases) jq -r "$filter" "$MOCK/releases.json" ;;
    */releases/*/assets)
        id=${path%/assets}; id=${id##*/}
        jq -r --argjson id "$id" '[.[] | select(.id == $id) | .assets[]]' \
            "$MOCK/releases.json" | jq -r "$filter" ;;
    */releases/*)
        # Published by a maintainer after the listing.
        [ ! -e "$MOCK/published-now" ] || { echo false; exit 0; }
        id=${path##*/}
        # The release this job created was published after the listing.
        if [ -e "$MOCK/mine-published" ] && [ "$id" = "$(cat "$MOCK/created")" ]; then
            echo false; exit 0
        fi
        jq -r --argjson id "$id" '.[] | select(.id == $id)' \
            "$MOCK/releases.json" | jq -r "$filter" ;;
    *) echo "fake gh: unexpected $path" >&2; exit 1 ;;
esac
EOF
chmod +x "$work/bin/gh"
echo zip > "$work/raylibdoom-1.0.0-windows-x64.zip"

failures=0

# run NAME EXPECTED_EXIT RELEASES_JSON [FLAGS]: runs the script for tag
# v1.0.0 and leaves the recorded calls in $MOCK/calls. FLAGS are files
# that change the fake's behaviour (api-fails, published-now, race,
# mine-published).
run() {
    MOCK=$work/$1
    mkdir "$MOCK"
    : > "$MOCK/calls"
    for flag in ${4:-}; do : > "$MOCK/$flag"; done
    printf '%s\n' "$3" > "$MOCK/releases.json"
    set +e
    MOCK=$MOCK PATH="$work/bin:$PATH" GH_REPO=o/r \
        sh "$here/upload-to-draft.sh" v1.0.0 \
        "$work/raylibdoom-1.0.0-windows-x64.zip" > "$MOCK/out" 2>&1
    code=$?
    set -e
    if [ "$code" -ne "$2" ]; then
        echo "FAIL $1: exit $code, expected $2"; cat "$MOCK/out"
        failures=$((failures + 1))
    fi
}

# check NAME PATTERN COUNT: PATTERN occurs COUNT times in the calls.
check() {
    n=$(grep -c -e "$2" "$work/$1/calls" || true)
    if [ "$n" -ne "$3" ]; then
        echo "FAIL $1: '$2' called $n times, expected $3"
        sed 's/^/    /' "$work/$1/calls"
        failures=$((failures + 1))
    fi
}

no_writes() {
    check "$1" '-X DELETE' 0
    check "$1" '-X POST' 0
    check "$1" 'release create' 0
}

asset='{"id": 900, "name": "raylibdoom-1.0.0-windows-x64.zip"}'
other='{"id": 5, "tag_name": "v0.9.0", "draft": false, "html_url": "u5", "assets": []}'

# A published release is never changed, even when it has our asset.
run published 1 "[$other, {\"id\": 10, \"tag_name\": \"v1.0.0\", \"draft\": false, \"html_url\": \"u10\", \"assets\": [$asset]}]"
no_writes published

# Nor when a draft for the same tag exists as well.
run published-and-draft 1 "[{\"id\": 10, \"tag_name\": \"v1.0.0\", \"draft\": false, \"html_url\": \"u10\", \"assets\": []}, {\"id\": 11, \"tag_name\": \"v1.0.0\", \"draft\": true, \"html_url\": \"u11\", \"assets\": []}]"
no_writes published-and-draft

# An existing draft: the old asset is replaced.
run draft 0 "[$other, {\"id\": 10, \"tag_name\": \"v1.0.0\", \"draft\": true, \"html_url\": \"u10\", \"assets\": [$asset]}]"
check draft '-X DELETE repos/o/r/releases/assets/900' 1
check draft 'releases/10/assets?name=raylibdoom-1.0.0-windows-x64.zip' 1
check draft 'release create' 0

# No release yet: a draft is created and used.
run none 0 "[$other]"
check none 'release create v1.0.0 --draft' 1
check none 'releases/6/assets?name=' 1
check none '-X DELETE' 0

# Two drafts from a race: the oldest gets the upload, nothing is deleted.
run two-drafts 0 "[{\"id\": 12, \"tag_name\": \"v1.0.0\", \"draft\": true, \"html_url\": \"u12\", \"assets\": []}, {\"id\": 11, \"tag_name\": \"v1.0.0\", \"draft\": true, \"html_url\": \"u11\", \"assets\": []}]"
check two-drafts 'releases/11/assets?name=' 1
check two-drafts 'releases/12/assets?name=' 0
check two-drafts '-X DELETE' 0

# The draft is published between the listing and the upload.
run published-meanwhile 1 "[{\"id\": 10, \"tag_name\": \"v1.0.0\", \"draft\": true, \"html_url\": \"u10\", \"assets\": [$asset]}]" published-now
no_writes published-meanwhile

# This job creates a draft (6) but another job's older draft (1) wins:
# the upload goes to 1 and our duplicate is deleted.
run lost-race 0 "[$other]" race
check lost-race 'release create v1.0.0 --draft' 1
check lost-race '-X DELETE repos/o/r/releases/6$' 1
check lost-race 'releases/1/assets?name=' 1
check lost-race 'releases/6/assets?name=' 0

# Same, but our duplicate was published before the cleanup: it is left
# alone, and the upload still goes to the remaining draft.
run lost-race-published 0 "[$other]" "race mine-published"
check lost-race-published '-X DELETE repos/o/r/releases/6' 0
check lost-race-published 'releases/1/assets?name=' 1
check lost-race-published 'releases/6/assets?name=' 0

# The listing fails: that is not "no release", so nothing is created.
run api-fails 1 "[]" api-fails
no_writes api-fails

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "upload-to-draft.sh: all checks passed"
