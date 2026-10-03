#!/bin/sh
# Tests upload-release.sh and the release job's trigger in
# release-arch.yml against a fake gh that keeps its releases in a JSON
# file. Nothing talks to GitHub: no tag or release is created. Needs
# python3 (with PyYAML) and jq.
#
#   packaging/arch/test-upload-release.sh

set -eu

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir "$work/bin" "$work/dist"
pkg=raylibdoom-1.2.3-1-x86_64.pkg.tar.zst
echo pkg > "$work/dist/$pkg"
echo sum > "$work/dist/$pkg.sha256"

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
    env PATH="$work/bin:$PATH" MOCK_STATE="$state" GH_REPO=o/r \
        GITHUB_EVENT_NAME="$event" GITHUB_REF_TYPE="$reftype" "$@" \
        sh "$here/upload-release.sh" v1.2.3 "$work/dist"/* \
        > "$state/out.txt" 2>&1 || rc=$?
}
assets_of() {
    jq -r ".[] | select(.id == $2) | [.assets[].name] | sort | join(\" \")" \
        "$work/state-$1/releases.json"
}
both="$pkg $pkg.sha256"
no_mutations() {
    ! grep -Eq -- '-X (DELETE|POST)|^release create|MUTATED_PUBLISHED' \
        "$work/state-$1/calls.log"
}
show() { cat "$work/state-$1/out.txt" "$work/state-$1/calls.log"; }

draft='[{"id":9,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u9",
         "assets":[{"id":90,"name":"raylibdoom-windows.zip"}]}]'
pub='[{"id":7,"tag_name":"v1.2.3","draft":false,"html_url":"https://x/v1.2.3",
       "assets":[{"id":70,"name":"raylibdoom-windows.zip"}]}]'

# The release job's and the WAD step's conditions, evaluated for each
# trigger. Only a tag push and a workflow_dispatch given a tag reach the
# upload step, and both bundle the WAD.
python3 - "$top/.github/workflows/release-arch.yml" > "$work/cond.txt" <<'PY'
import re, sys, yaml
jobs = yaml.safe_load(open(sys.argv[1]))['jobs']
cond = jobs['release']['if']
steps = jobs['package']['steps']
wad_cond = [s for s in steps if s.get('name') == 'Shareware DOOM1.WAD'][0]['if']
def ev(cond, event, ref_type, tag, shareware=False):
    ctx = {'github.event_name': event, 'github.ref_type': ref_type,
           'inputs.tag': tag, 'inputs.shareware': shareware}
    expr = re.sub(r"[a-z_]+\.[a-z_]+", lambda m: repr(ctx[m.group(0)]), cond)
    expr = expr.replace('&&', ' and ').replace('||', ' or ').replace('!=', ' != ')
    return eval(expr)
# The release job checks every package for the WAD, so whenever it runs
# the package job must have fetched it, with the default inputs
# (shareware=false) too.
for name, args in [('dispatch-no-tag', ('workflow_dispatch', 'branch', '')),
                   ('dispatch-tag', ('workflow_dispatch', 'branch', 'v1.2.3')),
                   ('tag-push', ('push', 'tag', '')),
                   ('branch-push', ('push', 'branch', '')),
                   ('pull-request', ('pull_request', 'branch', ''))]:
    print(name, ev(cond, *args), ev(wad_cond, *args))
print('dispatch-shareware', ev(cond, 'workflow_dispatch', 'branch', '', True),
      ev(wad_cond, 'workflow_dispatch', 'branch', '', True))
check = [s for s in jobs['release']['steps'] if s.get('name') == 'Check'][0]
print('release-checks-wad', 'check-wad-in-package.sh' in check['run'])
# A dispatch with a tag builds the game from that tag with the
# packaging of the revision the workflow runs on: the checkout stays on
# that revision, and the tag is passed to make-package.sh as the source.
co = [s for s in steps if s.get('uses', '').startswith('actions/checkout')][0]
build = [s for s in steps if s.get('name') == 'Build'][0]
version = [s for s in steps if s.get('name') == 'Version'][0]['run']
print('checkout-ref', 'ref' not in co.get('with', {})
      and 'steps.version.outputs.source' in build['env']['SOURCE']
      and 'make-package.sh' in build['run'] and '$SOURCE' in build['run']
      and 'source=refs/tags/$INPUT_TAG' in version)
# ... and the older-tag build that exercises it runs on pull requests.
old = [s for s in steps if s.get('name') == 'Build an older tag']
print('old-tag-check', bool(old) and 'refs/tags/$OLD_TAG' in old[0]['run'])
PY
expect="dispatch-no-tag False False
dispatch-tag True True
tag-push True True
branch-push False False
pull-request False False
dispatch-shareware False True
release-checks-wad True
checkout-ref True
old-tag-check True"
if [ "$(cat "$work/cond.txt")" = "$expect" ]; then
    pass "release job runs only for a tag push or a dispatch with a tag, and both fetch the WAD"
else
    fail "release job condition:"; cat "$work/cond.txt"
fi

# 1. workflow_dispatch without a tag: the job is skipped (above); the
#    script would not release for a non-dispatch event either.
run pr pull_request branch "$draft"
[ $rc -ne 0 ] && [ ! -s "$work/state-pr/calls.log" ] \
    && pass "pull_request does not release" || { fail "pull_request: rc=$rc"; show pr; }
run branch push branch "$draft"
[ $rc -ne 0 ] && [ ! -s "$work/state-branch/calls.log" ] \
    && pass "branch push does not release" || { fail "branch push: rc=$rc"; show branch; }

# 2. workflow_dispatch with a draft tag: adds both files to that draft,
#    keeps its other assets, creates nothing.
run dispatch-draft workflow_dispatch branch "$draft"
if [ $rc -eq 0 ] && [ "$(assets_of dispatch-draft 9)" = "$both raylibdoom-windows.zip" ] \
   && [ "$(jq length "$work/state-dispatch-draft/releases.json")" = 1 ] \
   && ! grep -q '^release create' "$work/state-dispatch-draft/calls.log"; then
    pass "dispatch with a draft tag attaches to that draft"
else
    fail "dispatch-draft: rc=$rc"; show dispatch-draft
fi

# 3. workflow_dispatch with a published tag: fails, changes nothing.
run dispatch-pub workflow_dispatch branch "$pub"
if [ $rc -ne 0 ] && no_mutations dispatch-pub \
   && [ "$(assets_of dispatch-pub 7)" = "raylibdoom-windows.zip" ]; then
    pass "dispatch with a published tag is refused"
else
    fail "dispatch-pub: rc=$rc"; show dispatch-pub
fi

# 4. workflow_dispatch with a tag that has no release: fails, and does
#    not create one.
run dispatch-none workflow_dispatch branch '[]'
if [ $rc -ne 0 ] && no_mutations dispatch-none \
   && [ "$(jq length "$work/state-dispatch-none/releases.json")" = 0 ]; then
    pass "dispatch without a draft creates nothing"
else
    fail "dispatch-none: rc=$rc"; show dispatch-none
fi

# 5. Tag push, no release yet: creates the draft and uploads.
run fresh push tag '[]'
id=$(jq -r '.[0].id' "$work/state-fresh/releases.json")
if [ $rc -eq 0 ] && [ "$(jq length "$work/state-fresh/releases.json")" = 1 ] \
   && [ "$(assets_of fresh "$id")" = "$both" ]; then
    pass "tag push creates the draft and uploads"
else
    fail "fresh: rc=$rc"; show fresh
fi

# 6. Tag push race: another OS job's draft (older id) appears while this
#    one creates its own. Uploads go to the older draft; ours is deleted.
run race push tag '[]' MOCK_RACE=1
if [ $rc -eq 0 ] && [ "$(jq -r '[.[].id] | join(" ")' "$work/state-race/releases.json")" = 5 ] \
   && [ "$(assets_of race 5)" = "$both" ]; then
    pass "race: uploads to the oldest draft, deletes its own"
else
    fail "race: rc=$rc"; show race
fi

# 7. Tag push after publishing: fails and changes nothing.
run published push tag "$pub"
[ $rc -ne 0 ] && no_mutations published \
    && pass "tag push leaves a published release alone" \
    || { fail "published: rc=$rc"; show published; }

# 8. Published release plus a stray draft: still fails, touches neither.
mixed='[{"id":7,"tag_name":"v1.2.3","draft":false,"html_url":"https://x/v1.2.3","assets":[]},
        {"id":8,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u8","assets":[]}]'
run mixed workflow_dispatch branch "$mixed"
[ $rc -ne 0 ] && no_mutations mixed \
    && pass "published release with a stray draft is left alone" \
    || { fail "mixed: rc=$rc"; show mixed; }

# 9. An old copy of the package in the draft is replaced.
old='[{"id":9,"tag_name":"v1.2.3","draft":true,"html_url":"https://x/u9",
       "assets":[{"id":90,"name":"'$pkg'"}]}]'
run clobber workflow_dispatch branch "$old"
if [ $rc -eq 0 ] && grep -q 'DELETE repos/o/r/releases/assets/90' "$work/state-clobber/calls.log" \
   && [ "$(assets_of clobber 9)" = "$both" ]; then
    pass "existing asset replaced"
else
    fail "clobber: rc=$rc"; show clobber
fi

# 10. The draft is published while uploading: stops before changing it.
run midway workflow_dispatch branch "$draft" MOCK_PUBLISH_AT=1
if [ $rc -ne 0 ] && ! grep -q MUTATED_PUBLISHED "$work/state-midway/calls.log" \
   && [ "$(assets_of midway 9)" = "$pkg raylibdoom-windows.zip" ]; then
    pass "publishing mid-upload stops further changes"
else
    fail "midway: rc=$rc"; show midway
fi

[ $fails -eq 0 ] || { echo "$fails failed"; exit 1; }
echo "all release upload tests passed"
