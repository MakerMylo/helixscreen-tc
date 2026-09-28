#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/subagent-brief.sh is the SubagentStart hook that puts the shared-machine
# rules into every subagent's context.

load helpers

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
}

@test "the block is a SubagentStart context entry" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh'
    [ "$status" -eq 0 ]
    [ "$(printf '%s' "$output" | jq -r '.hookSpecificOutput.hookEventName')" = "SubagentStart" ]
}

@test "the block carries the rules whose breach costs other sessions" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh | jq -r .hookSpecificOutput.additionalContext'
    contains "helix-claim take build:" "$output"
    contains '-j$(scripts/helix-claim jobs)' "$output"
    contains "zeus-run.sh" "$output"
    contains "foreground" "$output"
    contains "pkill" "$output"
}

@test "the block stays short enough not to crowd a brief" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh | jq -r .hookSpecificOutput.additionalContext | wc -c'
    [ "$output" -lt 1200 ]
}

@test "the wired hooks name scripts that exist" {
    run jq -r '.hooks.SubagentStart[].hooks[].command, (.hooks.PreToolUse[] | select(.matcher == "Bash") | .hooks[].command)' .claude/settings.json
    [ "$status" -eq 0 ]
    contains "scripts/subagent-brief.sh" "$output"
    contains "scripts/resource-advisor.sh" "$output"
    while read -r c; do
        c=${c#\"\$CLAUDE_PROJECT_DIR\"/}
        [ -x "${c%% *}" ]
    done <<< "$output"
}
