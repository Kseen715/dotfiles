# Repka Pi 3 — surviving `apt upgrade`

## The problem

A freshly flashed `Repka_26.08.0-trunk_Repkapi3_trixie_current_6.18.48.img.xz` boots
fine. Run `apt update && apt upgrade`, reboot, and the board is dead — no console,
no SSH, nothing.

## Root cause

The image is a **user-built** Armbian (`IMAGE_TYPE=user-built`, `BOARD_TYPE=csc`,
`VENDOR=Repka`). Its board support packages are versioned `26.08.0-trunk`:

| package | version | contains |
|---|---|---|
| `linux-image-current-sunxi64` | 26.08.0-trunk | kernel **6.18.48** |
| `linux-dtb-current-sunxi64`   | 26.08.0-trunk | **`allwinner/sun50i-h5-repka-pi3.dtb`** |
| `linux-u-boot-repkapi3-current` | 26.08.0-trunk | U-Boot for the board |
| `armbian-bsp-cli-repkapi3-current` | 26.08.0-trunk | `boot.cmd`, board scripts |

The image also enables `http://apt.armbian.com trixie` (`o=Armbian`), which carries
the **official generic sunxi64** packages of the same names at version **`26.8.3`**.

`dpkg --compare-versions`: `26.08.0-trunk` < `26.8.3` (the third field, `0` vs `3`,
decides). So `apt upgrade` considers the official packages an upgrade and installs them:

- `/boot/dtb` → `dtb-6.18.44-current-sunxi64`, which contains **89 Allwinner DTBs and
  zero Repka ones** (verified: `dpkg-deb -c linux-dtb-current-sunxi64_26.8.3_arm64.deb | grep -c repka` → `0`)
- `/boot/Image` → `vmlinuz-6.18.44-current-sunxi64`
- `/boot/armbianEnv.txt` still says `fdtfile=allwinner/sun50i-h5-repka-pi3.dtb`
- `linux-u-boot-repkapi3-current` and `armbian-bsp-cli-repkapi3-current` are *not*
  upgraded — those board packages don't exist upstream at all

At boot, U-Boot's `boot.cmd` looks for `${fdtdir}/${fdtfile}`, exhausts its fallbacks,
finds no Repka device tree anywhere, and stops. Nothing reaches the kernel, so there
is no console output and no network. Repka Pi 3 support simply is not in mainline Armbian.

## The fix

Freeze the four board-specific packages; let everything else (Debian security
updates, userland, `armbian-config`) upgrade normally.

```bash
sudo ./repka-pin-bsp.sh
```

This applies, idempotently:

1. `apt-mark hold` on the installed BSP packages
2. `/etc/apt/preferences.d/99-repka-bsp-pin` — `Pin-Priority: -1` against `release o=Armbian`
   for those package names (a hold can be bypassed with `--allow-change-held-packages`;
   a negative pin cannot be reached by the resolver at all)
3. `/usr/local/sbin/repka-boot-check` plus an APT `DPkg::Post-Invoke` hook, so any
   future apt run that removes the DTB, kernel, initrd, or boot script screams
   **before** you reboot
4. re-runs `apt-get update` and re-simulates `full-upgrade`, and **fails loudly** if
   any BSP package would still be touched

Add `--disable-armbian-repo` to turn `apt.armbian.com` off entirely — the most
conservative option if you don't need Armbian userland updates. It appends
`Enabled: no` as a **field of each stanza** in `armbian.sources`. This detail
matters: in deb822 a blank line terminates a stanza, so putting a newline before
the field makes it an orphan stanza and apt refuses to read *any* source:

```
E: Malformed stanza 2 in source list /etc/apt/sources.list.d/armbian.sources (type)
E: The list of sources could not be read.
```

On a live board the script re-parses the file afterwards and rolls the original
back if apt can no longer read it, so a bad edit can't leave you without apt.

### Verified on `rpi-glr-02`

- before pinning: `26 upgraded`, including `linux-image`/`linux-dtb` → 26.8.3
- after pinning: `24 upgraded`, kernel and DTB excluded
- ran the real `apt full-upgrade`, rebooted → **board came back**, still
  `6.18.48-current-sunxi64` with `sun50i-h5-repka-pi3.dtb`

## Patching the image itself (recommended for a fleet)

Rather than fixing each board after flashing, bake the pin into the image once.
Every card you write is then protected from its very first boot.

```bash
# .img.xz in, .img.xz out
TMPDIR=/var/tmp ./repka-patch-image.sh \
  ~/Downloads/Repka_26.08.0-trunk_Repkapi3_trixie_current_6.18.48.img.xz
# -> Repka_26.08.0-trunk_Repkapi3_trixie_current_6.18.48-pinned.img.xz
```

It needs **no root**. Instead of loop-mounting, it carves partition 1 out of the
image, edits it offline with `debugfs`, and writes it back. Into the rootfs it puts:

- `/etc/apt/preferences.d/99-repka-bsp-pin` — the `Pin-Priority: -1` pin
- `/usr/local/sbin/repka-boot-check` (mode 0755, root:root)
- `/etc/apt/apt.conf.d/99-repka-boot-check` — the post-invoke hook
- `hold` on the BSP packages, written straight into `/var/lib/dpkg/status`

Options: `-o/--output`, `--xz`/`--no-xz`, `--level N`, `--disable-armbian-repo`,
`--force`, `--keep-tmp`.

It refuses to touch an image whose `BOARD` is not `repkapi3` or that has no
`sun50i-h5-repka-pi3.dtb`, and it **verifies before emitting output** — if any check
fails, no output file is written at all.

### Notes

- Needs scratch space for the decompressed image plus a copy of its root partition
  (~4.8 GiB for this image). It checks upfront and tells you if you're short.
  **`/tmp` is tmpfs (RAM) on this machine and is too small — use `TMPDIR=/var/tmp`.**
- Re-running it on an already-patched image is a no-op that still verifies; it
  reports which packages were already held.
- Everything before partition 1 — the MBR and the raw U-Boot area at 8 KiB–4 MiB —
  is left byte-for-byte identical (verified by checksum).
- `xz` recompression is by far the slowest step. `--level 0` is fine for images you
  flash locally; the default 6 matches the original.

### Verified

Patched the real `Repka_26.08.0-...6.18.48.img.xz` end to end:

- all checks pass; output rootfs is `e2fsck`-clean
- 4 BSP packages held, pin file and hook present, boot check root-owned mode 0755
- `sun50i-h5-repka-pi3.dtb` intact
- pre-partition area byte-identical to the original (`sha256` match)
- `.img` → `.img.xz` roundtrip: `xz -t` clean, fsck clean, pin and DTB still there
- second run over its own output: idempotent
- `--disable-armbian-repo`: the rewritten `armbian.sources` was checked in a
  `debian:trixie` container — it parses, and `apt-get update --print-uris` drops
  from 3 armbian URIs to 0, while `debian.sources` is left untouched

## Provisioning several boards

```bash
# just pin
./repka-fleet-provision.sh rpi-glr-02 rpi-glr-03 rpi-glr-04

# or from a file, and do the full safe upgrade + reboot check on each
./repka-fleet-provision.sh --hosts hosts.txt --upgrade --reboot
```

Hosts are handled independently; a failure on one is reported and the run continues.
With `--reboot` it waits for the host to come back and prints the running kernel, so
a board that fails to return is reported rather than silently lost. It will refuse to
reboot a host whose boot files fail the sanity check after upgrading.

Requires key-based SSH and passwordless `sudo` (or `User root`, as in your
`~/.ssh/config`). If you patched the image with `repka-patch-image.sh`, the boards
are already pinned and this is only a verification pass.

## Recovering a board you already bricked

Reflashing works. To repair in place and keep the card's data:

```bash
sudo ./repka-recover-sd.sh \
  --image ~/Downloads/Repka_26.08.0-trunk_Repkapi3_trixie_current_6.18.48.img.xz \
  --device /dev/sdX
```

It loop-mounts the pristine image read-only, backs up the card's `/boot` to
`/boot.broken-<timestamp>`, rsyncs the BSP back, keeps the card's own
`armbianEnv.txt` (so the root UUID stays correct) while forcing `fdtfile` back to the
image's value, repoints the `Image`/`dtb`/`uInitrd` symlinks, and pre-installs the pin
file and dpkg holds so the card can't re-break on first boot.

It refuses to touch a device that hosts your running root, warns on a
non-removable target, and requires typing `YES`.

`--uboot` additionally restores the raw U-Boot area (8 KiB → start of partition 1,
which is 4 MiB on this image). You normally **don't** need it: `linux-u-boot-repkapi3-current`
has no upstream counterpart, so the upgrade never replaces U-Boot. The script
computes the bound from the actual partition table and refuses to overlap partition 1.

> Note: the pin, fleet, upgrade, and image-patch paths were verified end-to-end
> (the first three on live hardware, the patcher against the real image).
> `repka-recover-sd.sh` has had its image-side assumptions checked against the real
> `.img.xz` (single ext4 partition at sector 8192), but the write path has not been
> exercised on an actually-bricked card — take the `/boot.broken-*` backup seriously
> the first time you run it.

## Checking a board by hand

```bash
cat /boot/armbianEnv.txt | grep fdtfile
ls -l /boot/dtb/allwinner/sun50i-h5-repka-pi3.dtb   # must exist
apt-mark showhold                                   # must list the 4 BSP packages
apt-get -s full-upgrade | grep -E '^Inst linux-(image|dtb)'   # must print nothing
```
