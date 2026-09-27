#!/bin/sh
# Tests upload-release.sh and the release job's trigger against a fake
# gh that keeps its releases in a JSON file. Nothing talks to GitHub:
# no tag or release is created. Needs python3 and jq.
#
#   packaging/linux/test-upload-release.sh

set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir "$work/bin" "$work/dist"
echo deb > "$work/dist/raylibdoom_1.2.3_amd64.deb"
echo tgz > "$work/dist/raylibdoom-1.2.3-linux-x86_64.tar.gz"

cat > "$work/bin/gh" <<'EOF'
#!/usr/bin/env python3
# Fake gh: "gh api" and "gh release create" on $MOCK_STATE/releases.json.
# Every call goes to calls.log; a change to a published release is
# logged as MUTATED_PUBLISHED.
import json, os, re, subprocess, sys
state = os.environ['MOCK_STATE']
db = os.path.join(state, 'releases.json')
rels = json.load(open(db))
args = sys.argv[1:]
log = open(os.path.join(state, 'calls.log'), 'a')
log.write(' '.join(args) + '\n')

def save():
    json.dump(rels, open(db, 'w'))
def find(i):
    return next((r for r in rels if r['id'] == i), None)
def emit(value, jq):
    text = json.dumps(value)
    if jq:
        text = subprocess.run(['jq', '-r', jq], input=text, text=True,
                              capture_output=True, check=True).stdout
    sys.stdout.write(text if text.endswith('\n') else text + '\n')
def touch(r):
    if r and not r['draft']:
        log.write('MUTATED_PUBLISHED %d\n' % r['id'])

if args[:2] == ['release', 'create']:
    tag = args[2]
    if os.environ.get('MOCK_RACE'):
        # Another OS job created its draft a moment earlier.
        rels.append(dict(id=5, tag_name=tag, draft=True,
                         html_url='https://x/untagged-other', assets=[]))
    n = max([r['id'] for r in rels] + [10]) + 1
    url = 'https://x/untagged-%d' % n
    rels.append(dict(id=n, tag_name=tag, draft=True, html_url=url, assets=[]))
    save(); print(url); sys.exit(0)

assert args[0] == 'api', args
method, jq, endpoint, rest = 'GET', None, None, args[1:]
i = 0
while i < len(rest):
    a = rest[i]
    if a == '-X': method = rest[i + 1]; i += 2
    elif a == '--jq': jq = rest[i + 1]; i += 2
    elif a in ('-H', '--input'): i += 2
    elif a == '--paginate': i += 1
    else: endpoint = a; i += 1
path = re.sub(r'^https://uploads\.github\.com/', '', endpoint)
path, _, query = path.partition('?')
parts = path.split('/')[3:]          # after repos/<owner>/<repo>

if parts == ['releases']:
    emit(rels, jq)
elif len(parts) == 2 and method == 'GET':
    r = find(int(parts[1]))
    at = os.environ.get('MOCK_PUBLISH_AT')
    if r and at:
        # A maintainer publishes the draft after this many lookups.
        counter = os.path.join(state, 'lookups')
        n = int(open(counter).read()) + 1 if os.path.exists(counter) else 1
        open(counter, 'w').write(str(n))
        if n > int(at):
            r['draft'] = False; save()
    emit(r, jq)
elif len(parts) == 2 and method == 'DELETE':
    touch(find(int(parts[1])))
    rels[:] = [r for r in rels if r['id'] != int(parts[1])]; save()
elif parts[1] == 'assets' and method == 'DELETE':
    for r in rels:
        if any(a['id'] == int(parts[2]) for a in r['assets']):
            touch(r)
            r['assets'] = [a for a in r['assets'] if a['id'] != int(parts[2])]
    save()
elif parts[2] == 'assets' and method == 'GET':
    emit(find(int(parts[1]))['assets'], jq)
elif parts[2] == 'assets' and method == 'POST':
    r = find(int(parts[1])); touch(r)
    name = query.split('name=')[1]
    r['assets'].append(dict(id=1000 + len(r['assets']), name=name))
    save(); emit(dict(browser_download_url='https://x/' + name), jq)
else:
    sys.exit('fake gh: unhandled %s %s' % (method, endpoint))
EOF
chmod +x "$work/bin/gh"

fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

# run NAME EVENT REFTYPE INITIAL_JSON [VAR=VALUE...]: runs the upload
# script in a fresh state; leaves its exit status in $rc.
run() {
    name=$1 event=$2 reftype=$3 initial=$4
    shift 4
    state=$work/state-$name
    mkdir -p "$state"
    echo "$initial" > "$state/releases.json"
    : > "$state/calls.log"
    rc=0
    env PATH="$work/bin:$PATH" MOCK_STATE="$state" GH_REPO=o/r TAG=v1.2.3 \
        GITHUB_EVENT_NAME="$event" GITHUB_REF_TYPE="$reftype" "$@" \
        sh "$here/upload-release.sh" "$work/dist" > "$state/out.txt" 2>&1 || rc=$?
}
assets_of() {
    jq -r ".[] | select(.id == $2) | [.assets[].name] | sort | join(\" \")" \
        "$work/state-$1/releases.json"
}
both="raylibdoom-1.2.3-linux-x86_64.tar.gz raylibdoom_1.2.3_amd64.deb"
no_mutations() {
    ! grep -Eq -- '-X (DELETE|POST)|^release create|MUTATED_PUBLISHED' \
        "$work/state-$1/calls.log"
}

# 1. workflow_dispatch on a tag: refuses before any gh call.
run dispatch workflow_dispatch tag '[]'
if [ $rc -ne 0 ] && [ ! -s "$work/state-dispatch/calls.log" ]; then
    pass "workflow_dispatch on a tag does not release"
else
    fail "workflow_dispatch on a tag: rc=$rc"; cat "$work/state-dispatch/calls.log"
fi

# 2. A branch push: refuses too.
run branch push branch '[]'
[ $rc -ne 0 ] && [ ! -s "$work/state-branch/calls.log" ] \
    && pass "branch push does not release" || fail "branch push: rc=$rc"

# 3. No release yet: creates a draft and uploads both files.
run fresh push tag '[]'
id=$(jq -r '.[0].id' "$work/state-fresh/releases.json")
if [ $rc -eq 0 ] && [ "$(jq length "$work/state-fresh/releases.json")" = 1 ] \
   && [ "$(assets_of fresh "$id")" = "$both" ]; then
    pass "creates the draft and uploads"
else
    fail "fresh: rc=$rc"; cat "$work/state-fresh/out.txt"
fi

# 4. Published release for the tag (a rerun after publishing): fails
#    and changes nothing.
pub='[{"id":7,"tag_name":"v1.2.3","draft":false,"html_url":"https://x/v1.2.3",
       "assets":[{"id":70,"name":"raylibdoom_1.2.3_amd64.deb"}]}]'
run published push tag "$pub"
if [ $rc -ne 0 ] && no_mutations published \
   && [ "$(assets_of published 7)" = "raylibdoom_1.2.3_amd64.deb" ]; then
    pass "published release is left alone"
else
    fail "published: rc=$rc"; cat "$work/state-published/calls.log"
fi

# 5. Published release plus a stray draft: still fails, touches neither.
mixed='[{"id":7,"tag_name":"v1.2.3","draft":false,"html_url":"https://x/v1.2.3","assets":[]},
        {"id":8,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u8","assets":[]}]'
run mixed push tag "$mixed"
[ $rc -ne 0 ] && no_mutations mixed \
    && pass "published release with a stray draft is left alone" \
    || fail "mixed: rc=$rc"

# 6. Existing draft with an old copy of one file: replaced (--clobber).
old='[{"id":9,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u9",
       "assets":[{"id":90,"name":"raylibdoom_1.2.3_amd64.deb"}]}]'
run clobber push tag "$old"
if [ $rc -eq 0 ] && grep -q 'DELETE repos/o/r/releases/assets/90' "$work/state-clobber/calls.log" \
   && [ "$(assets_of clobber 9)" = "$both" ] \
   && ! grep -q '^release create' "$work/state-clobber/calls.log"; then
    pass "existing draft: old asset replaced, no new draft"
else
    fail "clobber: rc=$rc"; cat "$work/state-clobber/out.txt"
fi

# 7. Race: another job's draft (older id) appears while this one creates
#    its own. Uploads go to the older draft; this job's draft is deleted.
run race push tag '[]' MOCK_RACE=1
if [ $rc -eq 0 ] && [ "$(jq -r '[.[].id] | join(" ")' "$work/state-race/releases.json")" = 5 ] \
   && [ "$(assets_of race 5)" = "$both" ]; then
    pass "race: uploads to the oldest draft, deletes its own"
else
    fail "race: rc=$rc"; cat "$work/state-race/out.txt"; cat "$work/state-race/releases.json"
fi

# 8. Draft published by a maintainer while uploading: stops before
#    changing the published release.
draft='[{"id":9,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u9","assets":[]}]'
run midway push tag "$draft" MOCK_PUBLISH_AT=1
if [ $rc -ne 0 ] && ! grep -q MUTATED_PUBLISHED "$work/state-midway/calls.log" \
   && [ "$(assets_of midway 9)" = "raylibdoom-1.2.3-linux-x86_64.tar.gz" ]; then
    pass "publishing mid-upload stops further changes"
else
    fail "midway: rc=$rc"; cat "$work/state-midway/calls.log"
fi

# 9. The release job itself only runs for tag pushes, so a
#    workflow_dispatch on a tag never reaches the upload step.
cond=$(python3 - "$top/.github/workflows/release-linux.yml" <<'EOF'
import sys, yaml
print(yaml.safe_load(open(sys.argv[1]))['jobs']['release']['if'])
EOF
)
case $cond in
    *"github.event_name == 'push'"*"github.ref_type == 'tag'"*)
        pass "release job condition: $cond" ;;
    *) fail "release job condition does not require a tag push: $cond" ;;
esac

[ $fails -eq 0 ] || { echo "$fails failed"; exit 1; }
echo "all release upload tests passed"
