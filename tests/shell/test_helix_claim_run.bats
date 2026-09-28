#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `helix-claim run heavy:<what> -- cmd` holds a claim for exactly as long as the
# command runs, owned by the wrapper, and passes the command's status through.
# `resources` is a read-only snapshot that must never hang on an unreachable zeus.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"

setup() {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/claims"
    mkdir -p "$HELIX_CLAIM_DIR"
    CF="$HELIX_CLAIM_DIR/heavy_t.json"
}

teardown() {
    for p in ${OWNER:-} ${WRAP:-}; do kill "$p" 2>/dev/null; done
    return 0
}

wait_for_file() {
    for _ in $(seq 50); do [ -s "$1" ] && return 0; sleep 0.1; done
    return 1
}

@test "run releases the claim after a normal exit" {
    run "$CLAIM" run heavy:t -- true
    [ "$status" -eq 0 ]
    [ ! -e "$CF" ]
}

@test "run passes a non-zero exit through and still releases" {
    run "$CLAIM" run heavy:t --note n -- sh -c 'exit 7'
    [ "$status" -eq 7 ]
    [ ! -e "$CF" ]
}

@test "the claim is held while the command runs" {
    run "$CLAIM" run heavy:t -- sh -c "cat '$CF'"
    [ "$status" -eq 0 ]
    contains '"resource": "heavy:t"' "$output"
}

@test "SIGTERM to the wrapper stops the command and releases" {
    "$CLAIM" run heavy:t -- sh -c "echo \$\$ > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    [ -e "$CF" ]
    kill -TERM "$WRAP"
    local rc=0; wait "$WRAP" || rc=$?
    [ "$rc" -eq 143 ]
    [ ! -e "$CF" ]
    ! kill -0 "$(cat "$BATS_TEST_TMPDIR/child")" 2>/dev/null
}

@test "a refused take leaves the command unrun" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take heavy:t "someone else" --pid "$OWNER" >/dev/null
    run "$CLAIM" run heavy:t -- touch "$BATS_TEST_TMPDIR/ran"
    [ "$status" -ne 0 ]
    contains "REFUSED" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ran" ]
    contains "someone else" "$(cat "$CF")"
}

@test "list shows the process-tree RSS of a live heavy claim" {
    "$CLAIM" run heavy:t -- sh -c "echo x > '$BATS_TEST_TMPDIR/child'; exec sleep 60" &
    WRAP=$!
    wait_for_file "$BATS_TEST_TMPDIR/child"
    run "$CLAIM" list
    contains "heavy:t" "$output"
    contains "rss=" "$output"
}

@test "list shows no RSS for a claim that is not heavy" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take device:x "hw" --pid "$OWNER" >/dev/null
    run "$CLAIM" list
    lacks "rss=" "$output"
}

@test "resources with zeus unreachable prints thelio and exits 0" {
    sleep 120 &
    OWNER=$!
    "$CLAIM" take device:x "hw" --pid "$OWNER" --note "192.0.2.7" >/dev/null
    ZEUS_HOST=zeus.invalid run timeout 10 "$CLAIM" resources
    [ "$status" -eq 0 ]
    contains "fair share -j" "$output"
    contains "device:x" "$output"
    contains "192.0.2.7" "$output"
    contains "memory by command" "$output"
    contains "zeus (zeus.invalid): unreachable" "$output"
}

@test "resources --no-zeus never reaches for zeus" {
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
    run "$CLAIM" resources --no-zeus
    [ "$status" -eq 0 ]
    lacks "zeus" "$output"
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}

@test "resources cuts off a zeus that hangs and still exits 0" {
    mock_command_script ssh 'exec sleep 30'
    SECONDS=0
    run timeout 15 "$CLAIM" resources
    [ "$status" -eq 0 ]
    [ "$SECONDS" -le 11 ]
    contains "unreachable" "$output"
}
