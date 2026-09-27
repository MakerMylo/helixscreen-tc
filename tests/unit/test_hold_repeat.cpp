// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_hold_repeat.cpp
 * @brief Hold-to-repeat: timing decisions, the shared LVGL binding, and the
 *        blocked-jog stop at panel level.
 *
 * The pure HoldRepeat tests run without LVGL on purpose: the unit-test
 * harness only executes one-shot timers, never the periodic lv_timer a real
 * hold runs on, so everything time-based is driven with explicit
 * elapsed-millisecond values through HoldRepeat::on_elapsed() and
 * HoldRepeatTimer::poll().
 */

#include "hold_repeat.h"
#include "hold_repeat_timer.h"

#include "../catch_amalgamated.hpp"

using helix::HoldRepeat;

TEST_CASE("hold repeat fires nothing before the delay", "[hold_repeat]") {
    HoldRepeat hr;
    hr.press();
    CHECK(hr.active());
    CHECK_FALSE(hr.on_elapsed(0));
    CHECK_FALSE(hr.on_elapsed(HoldRepeat::DELAY_MS - 1));
    CHECK(hr.on_elapsed(HoldRepeat::DELAY_MS));
    CHECK(hr.swallow_click());
}

TEST_CASE("hold repeat cadence after the first fire", "[hold_repeat]") {
    HoldRepeat hr;
    hr.press();
    REQUIRE(hr.on_elapsed(HoldRepeat::DELAY_MS));
    // One repeat per INTERVAL_MS, anchored on the press.
    CHECK_FALSE(hr.on_elapsed(HoldRepeat::DELAY_MS + HoldRepeat::INTERVAL_MS - 1));
    CHECK(hr.on_elapsed(HoldRepeat::DELAY_MS + HoldRepeat::INTERVAL_MS));
    CHECK_FALSE(hr.on_elapsed(HoldRepeat::DELAY_MS + 2 * HoldRepeat::INTERVAL_MS - 1));
    CHECK(hr.on_elapsed(HoldRepeat::DELAY_MS + 2 * HoldRepeat::INTERVAL_MS));
}

TEST_CASE("a tap with no repeat jogs once via the click path", "[hold_repeat]") {
    HoldRepeat hr;
    hr.press();
    CHECK_FALSE(hr.on_elapsed(250));
    hr.release();
    CHECK_FALSE(hr.swallow_click());
    // Nothing fires after release even if polled late.
    CHECK_FALSE(hr.on_elapsed(5000));
}

TEST_CASE("release swallows the click only after a repeat fired", "[hold_repeat]") {
    HoldRepeat hr;
    // Released without any repeat: the click goes through.
    hr.press();
    hr.release();
    CHECK_FALSE(hr.swallow_click());
    // Released after one repeat: the click must not jog again.
    hr.press();
    REQUIRE(hr.on_elapsed(HoldRepeat::DELAY_MS));
    hr.release();
    CHECK(hr.swallow_click());
}

TEST_CASE("cancel stops ticks and forgets the repeat", "[hold_repeat]") {
    HoldRepeat hr;
    hr.press();
    REQUIRE(hr.on_elapsed(HoldRepeat::DELAY_MS));
    hr.cancel();
    CHECK_FALSE(hr.active());
    CHECK_FALSE(hr.on_elapsed(HoldRepeat::DELAY_MS + HoldRepeat::INTERVAL_MS));
    CHECK_FALSE(hr.swallow_click());
}

TEST_CASE("a new press after cancel starts clean", "[hold_repeat]") {
    HoldRepeat hr;
    hr.press();
    REQUIRE(hr.on_elapsed(HoldRepeat::DELAY_MS));
    hr.cancel();
    hr.press();
    // The full delay applies again and no stale swallow carries over.
    CHECK(hr.active());
    CHECK_FALSE(hr.swallow_click());
    CHECK_FALSE(hr.on_elapsed(HoldRepeat::DELAY_MS - 1));
    REQUIRE(hr.on_elapsed(HoldRepeat::DELAY_MS));
    CHECK(hr.swallow_click());
}

// ============================================================================
// The shared LVGL binding
// ============================================================================

#include "../lvgl_test_fixture.h"

namespace {

struct FireCtx {
    int fires = 0;
    bool allow = true;

    bool fire() {
        ++fires;
        return allow;
    }
};

bool ctx_fire(void* user_data) {
    return static_cast<FireCtx*>(user_data)->fire();
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "HoldRepeatTimer fires on poll and stops when refused",
                 "[hold_repeat][ui]") {
    helix::HoldRepeatTimer timer;
    FireCtx ctx;

    timer.begin(&ctx_fire, &ctx);
    REQUIRE(timer.ticking());

    // Below the delay: nothing fires, ticking continues.
    CHECK_FALSE(timer.poll(HoldRepeat::DELAY_MS - 1));
    CHECK(ctx.fires == 0);
    CHECK(timer.ticking());

    // First repeat fires; the owner allows more.
    CHECK(timer.poll(HoldRepeat::DELAY_MS));
    REQUIRE(ctx.fires == 1);
    CHECK(timer.ticking());
    CHECK(timer.swallow_click());

    // The owner refuses (jog blocked): ticking stops, but the repeat that
    // already fired still swallows the imminent click.
    ctx.allow = false;
    CHECK(timer.poll(HoldRepeat::DELAY_MS + HoldRepeat::INTERVAL_MS));
    REQUIRE(ctx.fires == 2);
    CHECK_FALSE(timer.ticking());
    // A stopped timer polls nothing further, whatever the clock says.
    CHECK_FALSE(timer.poll(60'000));
    CHECK(ctx.fires == 2);
    CHECK(timer.swallow_click());

    timer.cancel();
    CHECK_FALSE(timer.swallow_click());
}

TEST_CASE_METHOD(LVGLTestFixture, "HoldRepeatTimer release stops ticks and keeps the swallow",
                 "[hold_repeat][ui]") {
    helix::HoldRepeatTimer timer;
    FireCtx ctx;

    timer.begin(&ctx_fire, &ctx);
    CHECK(timer.poll(HoldRepeat::DELAY_MS));
    REQUIRE(ctx.fires == 1);

    timer.release();
    CHECK_FALSE(timer.ticking());
    CHECK(timer.swallow_click());
    CHECK_FALSE(timer.poll(HoldRepeat::DELAY_MS + HoldRepeat::INTERVAL_MS));
    CHECK(ctx.fires == 1);

    timer.cancel();
    CHECK_FALSE(timer.swallow_click());
}
