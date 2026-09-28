#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/resource-advisor.sh runs as a PreToolUse hook on every Bash call. It
# never blocks: it answers with a suggestion, or with nothing. The share and the
# memory figure come from `helix-claim jobs -v`, stubbed here through
# HELIX_ADVISOR_JOBS_CMD so each case controls how busy the box looks.

load helpers

ADVISOR="scripts/resource-advisor.sh"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    TEST_DIR="$(mktemp -d)"
    export HELIX_ADVISOR_JOBS_CMD="$TEST_DIR/jobs"
    export JOBS_CALLED="$TEST_DIR/jobs.called"
    roomy
}

teardown() {
    rm -rf "$TEST_DIR"
}

# A `helix-claim jobs -v` stand-in: the -v line on stderr, the share on stdout.
stub_jobs() {
    local share="$1" avail="$2" extra="${3:-}"
    cat > "$HELIX_ADVISOR_JOBS_CMD" <<EOF
#!/usr/bin/env bash
touch "$JOBS_CALLED"
echo "ncpu=32 peers=1(claimed+inferred) cc1plus=0 availGB=$avail -> -j$share$extra" >&2
echo "$share"
EOF
    chmod +x "$HELIX_ADVISOR_JOBS_CMD"
}
roomy() { stub_jobs 16 60; }
tight_share() { stub_jobs 6 60; }
tight_memory() { stub_jobs 16 12; }

advise() {
    local json
    json=$(jq -cn --arg c "$1" '{tool_name: "Bash", tool_input: {command: $c}}')
    run bash -c "printf '%s' \"\$1\" | $ADVISOR" _ "$json"
}

context() {
    printf '%s' "$output" | jq -r '.hookSpecificOutput.additionalContext'
}

# ---------------------------------------------------------------------------
# Silence and speed on ordinary commands
# ---------------------------------------------------------------------------

@test "an ordinary command gets no output and never reads the box" {
    advise "git status --short"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
    [ ! -e "$JOBS_CALLED" ]
}

@test "a single-tag test run is not heavy" {
    tight_share
    advise "make t F='[ams]'"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "a command already on zeus is left alone" {
    tight_share
    advise "scripts/zeus-run.sh mutate --tests '[ams]'"
    [ -z "$output" ]
    advise "ssh zeus.local 'sudo -n docker exec helix-tsan make full-test-run'"
    [ -z "$output" ]
}

# ---------------------------------------------------------------------------
# Always on zeus, however quiet thelio is
# ---------------------------------------------------------------------------

@test "addr2line against helix-tests is sent to zeus on a roomy box" {
    advise "addr2line -f -C -e build/bin/helix-tests 0x69125f5"
    [ "$status" -eq 0 ]
    contains "zeus" "$(context)"
    contains "addr2line" "$(context)"
}

@test "gdb against a helix binary points at coredumpctl" {
    advise "gdb -batch -ex bt build/bin/helix-screen core.123"
    contains "coredumpctl info" "$(context)"
}

@test "a full gate is sent to zeus's sweep mode on a roomy box" {
    printf '#!/usr/bin/env bash\ncase "$1" in\n    sweep)\n        ;;\nesac\n' > "$TEST_DIR/zeus-run.sh"
    export HELIX_ADVISOR_ZEUS_RUN="$TEST_DIR/zeus-run.sh"
    advise "make -j full-test-run"
    contains "zeus-run.sh sweep" "$(context)"
    contains "push the branch" "$(context)"
}

@test "a full gate names no zeus mode that zeus-run lacks" {
    printf '#!/usr/bin/env bash\ncase "$1" in\n    test)\n        ;;\nesac\n' > "$TEST_DIR/zeus-run.sh"
    export HELIX_ADVISOR_ZEUS_RUN="$TEST_DIR/zeus-run.sh"
    advise "make unit-sweep"
    contains "helix-claim jobs -v" "$(context)"
    lacks "zeus-run.sh sweep" "$(context)"
    lacks "zeus-run.sh full" "$(context)"
}

@test "a mutation run is sent to zeus" {
    advise "make mutate-diff"
    contains "zeus-run.sh mutate" "$(context)"
}

# ---------------------------------------------------------------------------
# Only when thelio is tight
# ---------------------------------------------------------------------------

@test "an explicit -j above the fair share is flagged" {
    tight_share
    advise "make -j24"
    contains '-j$(scripts/helix-claim jobs)' "$(context)"
    contains "-j6" "$(context)"
}

@test "an explicit -j at or under the fair share is not flagged" {
    tight_share
    advise "make -j4 test"
    [ -z "$output" ]
}

@test "a docker cross build is sent to zeus only when thelio is tight" {
    advise "make snapmaker-u1-docker"
    [ -z "$output" ]
    tight_memory
    advise "make snapmaker-u1-docker"
    contains "zeus" "$(context)"
}

@test "an idf build in docker is heavy when thelio is tight" {
    tight_share
    advise "docker run --rm -v \$PWD:/src espressif/idf idf.py build"
    contains "zeus" "$(context)"
}

@test "a test binary in a loop is flagged only when thelio is tight" {
    local loop='for i in $(seq 1 200); do ./build/bin/helix-tests "[x]" || f=$((f+1)); done'
    advise "$loop"
    [ -z "$output" ]
    tight_memory
    advise "$loop"
    contains "loop" "$(context)"
}

@test "memory under 16GB counts as tight even with a wide share" {
    tight_memory
    advise "make -j24"
    contains "12GB" "$(context)"
}

# ---------------------------------------------------------------------------
# Contract with Claude Code and with helix-claim
# ---------------------------------------------------------------------------

@test "a suggestion is a PreToolUse context block that does not decide permission" {
    advise "make mutate-diff"
    [ "$status" -eq 0 ]
    [ "$(printf '%s' "$output" | jq -r '.hookSpecificOutput.hookEventName')" = "PreToolUse" ]
    [ "$(printf '%s' "$output" | jq -r '.hookSpecificOutput.permissionDecision // "none"')" = "none" ]
}

@test "extra trailing fields on the jobs line are ignored" {
    stub_jobs 6 60 " shards=3 nice=10"
    advise "make -j24"
    contains "-j6" "$(context)"
}

@test "a missing helix-claim still exits 0 and still sends symbolizers to zeus" {
    export HELIX_ADVISOR_JOBS_CMD="$TEST_DIR/does-not-exist"
    advise "make -j24"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
    advise "addr2line -e build/bin/helix-tests 0x1"
    [ "$status" -eq 0 ]
    contains "zeus" "$(context)"
}

@test "garbage on stdin exits 0 with no output" {
    run bash -c "printf 'not json' | $ADVISOR"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "a plain test-binary run does not read the box" {
    advise "./build/bin/helix-tests '[ams]'"
    [ -z "$output" ]
    [ ! -e "$JOBS_CALLED" ]
}
