#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# SubagentStart hook (wired in .claude/settings.json). Injects the shared-machine
# rules into every subagent's context, so a brief cannot forget them. A subagent
# reads none of its parent's memory and briefs already run long, so the block
# carries only the rules whose breach costs the other sessions on this box.

block='Shared machine: thelio (32 threads) is used by several sessions at once; zeus has 2x the RAM and is usually idle.
- Before building or running tests: `scripts/helix-claim take build:<tree> "<why>"`. If REFUSED, stop and report. Release it when done.
- `-j$(scripts/helix-claim jobs)`, never a fixed -j. `scripts/helix-claim jobs -v` shows how busy thelio is.
- Full gates, mutation, ASAN and symbolizing go to zeus: `scripts/zeus-run.sh` (it runs pushed SHAs: push your branch, never main). Never addr2line or gdb against helix-tests on thelio.
- Run builds and tests in the foreground. You are not woken when a background job ends, so never go idle waiting on one. If a command times out into the background, kill its PID before moving on.
- Kill only PIDs you started; never pkill or pgrep -f by name.
- Report: a TODO list of what was asked, done / not done, first; then what you did NOT verify.'

jq -cn --arg c "$block" '{hookSpecificOutput: {hookEventName: "SubagentStart", additionalContext: $c}}'
