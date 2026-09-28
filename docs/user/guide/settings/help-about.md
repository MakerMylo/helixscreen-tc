# Settings: Help & About

**Settings > Help & About** is where to go when you need help, and where HelixScreen tells you about itself. Updates have their own page: see [Updates](updates.md).

---

## Help & About

Five items on the Help & About page:

| Action | What It Does |
|--------|--------------|
| **Replay Welcome Tour** | Plays the guided first-run tour again (see below) |
| **Upload Debug Bundle** | Collects logs and system info for support (see below) |
| **Discord Community** | Join **discord.gg/RZCT2StKhr** for community help and feedback |
| **Documentation** | Visit **helixscreen.org/docs** for guides and reference |
| **About** | Version, printer and system info, print hours (see below) |

### Welcome Tour

The first time you launch HelixScreen (after finishing the setup wizard), a short guided tour walks you through the interface. It's an eight-step overlay that highlights one part of the screen at a time, with a **Skip** button to leave early, a **Next** button to move on (it reads **Done** on the last step), and a step counter so you know how far along you are.

The tour covers:

1. **Welcome to HelixScreen** — a quick hello.
2. **Your printer at a glance** — tap any home tile to open its full controls or toggle its state.
3. **Customize your home screen** — long-press any tile to enter edit mode and rearrange, resize, remove, or add widgets.
4. **Print status** — monitor prints in progress and pause, resume, or cancel the active job.
5. **Controls** — move the toolhead, home axes, level the bed, and tune temperatures and fans.
6. **Filament** — load, unload, and swap spools, and monitor your multi-filament system.
7. **Advanced** — macros, the G-code console, calibration tools, and firmware updates.
8. **Settings** — network, display, sound, printer setup, and more.

The tour runs on the Home screen; tapping a navigation button to leave Home ends it early. It also reappears automatically after a HelixScreen update introduces new tour content.

### Replaying the Tour

To see it again, tap **Replay Welcome Tour** at the top of **Settings → Help & About**. HelixScreen returns to the Home screen and restarts the tour from the beginning. This row is always available, so you can revisit the tour whenever you like.

### Debug Bundles

When you need help troubleshooting an issue:

1. Tap **Upload Debug Bundle** in Settings
2. The bundle collects your logs, system info, and configuration (no personal data)
3. Tap **Upload** to send the bundle securely
4. Share the resulting code with the HelixScreen team on Discord or GitHub

Debug bundles include:

- **System logs** - recent HelixScreen log output, starting from the very beginning of startup. That matters for problems that happen while HelixScreen is still loading your settings - a settings file that could not be read, or one that had to be restored from backup. On older versions those messages happened before the log was being kept, so the bundle showed no trace of them even though you saw the message on screen
- **Configuration** — your HelixScreen settings (sanitized, no passwords or API keys)
- **Printer configuration** — your Klipper `printer.cfg` and any files it includes, so support can see how your printer is actually set up (sanitized, see below)
- **Installed macros** — the names of your G-code macros (names only, not what they do)
- **System info** — OS version, hardware details, display resolution
- **Crash data** — if a crash occurred, the crash report and backtrace
- **Crash history** — past crash submissions with their GitHub issue references (helps support identify recurring issues)
- **Device identifier** — a double-hashed ID used only for correlating telemetry data (not personally identifiable)

Debug bundles contain technical information needed for troubleshooting. Before anything is uploaded, HelixScreen strips passwords, API keys and tokens, web-hook URLs (Discord, Slack, Telegram, Pushover, ntfy, IFTTT), usernames and passwords embedded in URLs, email addresses, and MAC addresses.

One thing worth knowing: your `printer.cfg` is your own file, and HelixScreen can only redact patterns it recognizes. File paths are left intact, so an include pointing at `/home/yourname/…` will show that name. If you keep something unusual in your printer config that you would rather not share, look at it before you send the code — and remember a share code is only as private as the people you give it to.

**Which builds can upload.** Bundles leave the printer only on builds from the official releases. If you compiled HelixScreen yourself, the upload step stops with an "Upload unavailable in this build" message; setting `HELIX_DIAGNOSTIC_UPLOADS=1` in `helixscreen.env` turns uploads on for that install. Setting it to `0` turns them off on any build.

---

## About

Tap **About** at the bottom of the Help & About page to open the About overlay. It shows version and system information and HelixScreen branding.

| Item | Description |
|------|-------------|
| **HelixScreen Logo & Branding** | HelixScreen logo, "by Preston Brown", copyright notice, and a scrolling contributor marquee |
| **Printer Name** | The name of your connected printer (set during setup wizard) |
| **Current Version** | Your installed HelixScreen version |
| **Klipper** | Installed Klipper version (fetched from Moonraker) |
| **Moonraker** | Installed Moonraker version |
| **OS** | Operating system version |
| **Print Hours** | Total print hours tracked — tap to open the [History Dashboard](../advanced.md) |
| **Open Source Licenses** | View licenses for all open source libraries used by HelixScreen |

### Easter Eggs

- Tap the **Printer Name** row **seven times** to launch a hidden Snake game
- Tap the **Current Version** row **seven times** to toggle beta features — works like Android's "tap build number" developer mode

### Enabling Beta Features

Tap the **Current Version** row seven times in **Settings > Help & About > About** to toggle beta features.

When beta features are enabled:
- The **Update Channel** selector in [Updates](updates.md#update-channel) gains a third entry, **Dev**
- Additional rows appear in the Advanced panel (Configure PRINT_START, Tool Offsets, Belt Tension; some only on printers whose hardware supports them)
- Tap seven more times to disable

---

[Back to Settings](../settings.md) | [Prev: Updates](updates.md)
