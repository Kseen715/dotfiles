---
title: The win- modules
type: readme
tags:
  - kind/reference
  - topic/os-rice
  - topic/windows
  - lang/c
  - os/windows
---

# The `win-` modules

OS-level passes over a Windows machine: debloat, tweaks, updates —
`modules/win-tweaks.c`, `modules/win-update.c`, `modules/win-debloat.c`. This
is what the old `windows-11-x86_64/` PowerShell tree did, ported into the C
core and deleted.

These are **not** app modules. `modules/fastfetch.c` and its siblings install a
program and paint its config; nothing here installs anything or has a theme
layer — each one changes the operating system. That is what the `win-` prefix
says, and it is a prefix rather than a folder because every module in this repo
is one file in `modules/`, whichever operating systems it runs on (see
[[PLAN_UNIVERSAL]] ([[archive-decisions#D12|D12]])). They are asked for
by name, never listed in a `rice.list`. A `--theme-only` run treats every one
of them as a no-op, on purpose.

```powershell
.\osr.ps1 module win-tweaks     # services + Explorer/taskbar/snap + sudo
.\osr.ps1 module win-update     # ask Windows Update to run now
.\osr.ps1 module win-debloat    # Raphire's Win11Debloat  (third-party)
.\osr.ps1 module win-winutil    # Chris Titus WinUtil     (third-party)
```

> [!warning] Four modules, three files
> `win-winutil` has no `win-winutil.c`; it lives in `modules/win-debloat.c`
> beside `win-debloat`.

All four need Administrator for most of what they do and ask for it once, up
front, through `lib/elevate.c` — the same single UAC prompt the rest of a run
uses, instead of the unconditional self-elevating relaunch `setup.ps1` opened
with. `win-tweaks` degrades gracefully if you decline: the eleven per-user
(HKCU) settings still apply, the services and the machine-wide `sudo` row are
skipped with one warning.

## Not `win11`

The source tree was named `windows-11-x86_64/`, but almost nothing in it is
Windows 11 specific, so these modules are not named for 11 either:

| scope | rows |
| --- | --- |
| XP/Vista/7 era | `HideFileExt`, `Hidden`, `DontPrettyPath`, `DisallowShaking`; the `WSearch`, `SysMain`, `DPS`, `Fax`, `wuauserv` services |
| Windows 10+ | `SnapAssist`; the `DiagTrack` and `dmwappushservice` services; `usoclient` in `win-update.c`; both vendor tools in `win-debloat.c` |
| Windows 11 only | `EnableTaskGroups`, `EnableSnapBar`, `EnableSnapAssistFlyout`, `DITest`, `TaskbarEndTask`, and `sudo` (24H2+) |
| Windows 10 only | `ShowCortanaButton` — 11 has no such button and never reads it |

Rows apply unconditionally rather than being gated on a detected version. That
is safe in one direction only, which is the reason for the choice: writing an
Explorer value this build ignores, or creating the `Sudo` key on a build with
no sudo, does nothing at all — while guessing a version wrong and skipping a
setting the machine did want fails silently.

## Files

| file | was |
| --- | --- |
| `win-tweaks.c` | `setup.ps1` + `microscripts/reg-*.ps1` + `microscripts/disable-*.ps1` |
| `win-update.c` | `win-update.ps1` + `microscripts/update-windows.ps1` |
| `win-debloat.c` | `winutils.ps1` + `microscripts/raphire-win11debloat.ps1` — and `win-winutil` too: both vendor scripts are one file, since they differ only in which script is fetched |
| `../lib/wintweak.c` | `src/common.ps1`'s `UpdateRegistryValue`, `Stop-Service`/`Set-Service`, `Remove-Item -Recurse` |

`src/common.ps1`'s other half needed no port: its `EchoInfo`/`EchoWarning`/
`EchoError`/`InvokeEcho` were already `lib/ui.c`, and `Test-IsElevated` /
`Invoke-ElevatedScript` were already `lib/elevate.c`.

The twelve `reg-*.ps1` files differed from each other only in a key, a value
name and a default, and the six `disable-*.ps1` files only in a service name —
so they are now two tables in `win-tweaks.c` rather than eighteen files. Every
setting they carried is still there, including the two that were deliberately
switched off (`DontPrettyPath`, which `setup.ps1` never called, and the Fax
service, whose line was commented out). `test/unit_c/wintweak_test.c` asserts
the tables row by row, so a lost or flipped setting fails the build rather than
a desktop.

## `win-data/`

Two files the ps1 tree carried but never actually used from a script. Neither
is code, so neither was ported; both are kept because re-deriving a tweak
selection by hand is exactly the work this repo exists to avoid.

- **`ooshutup10.cfg`** — a saved profile for [O&O ShutUp10++]. That tool is not
  installed or driven by anything here; load this file in it (`Actions` →
  `Import settings`) or apply it headlessly with `OOSU10.exe ooshutup10.cfg
  /quiet`.
- **`winutils.json`** — a saved tweak selection for Chris Titus's WinUtil,
  matching what `win-winutil` launches. WinUtil imports it from its own
  `Settings` → `Import` menu. `win-winutil` does not pass it: the ps1 didn't
  either, and a config that silently applies a dozen system tweaks is something
  to click through, not something a module should hand over on your behalf.

[O&O ShutUp10++]: https://www.oo-software.com/en/shutup10
