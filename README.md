# Dell Broadcom ControlVault 3 (0a5c:5843) Fingerprint Reader for Debian

[![Debian](https://img.shields.io/badge/Debian-13%20(trixie)%20%7C%2012%20(bookworm)-red?logo=debian)](https://www.debian.org/)
[![Hardware](https://img.shields.io/badge/Hardware-Broadcom%200a5c%3A5843-blue)](https://linux-hardware.org/?id=usb:0a5c-5843)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-green.svg)](LICENSE)

An automated, one-step installer and protocol shim to enable the **Broadcom BCM58200 ControlVault 3** fingerprint reader (`0a5c:5843`) on **Debian Linux**.

Commonly found in:
* **Dell Latitude:** 5320, 5420, 5430, 5431, 5520, 5530, 7320, 7400, 7420, 7430, 7520, etc.
* **Dell Precision:** 3560, 3570, 5560, 5570, 7560, 7760, etc.

---

## The Problem

If you run `lsusb` on an affected Dell laptop, you will see:
```text
Bus 003 Device 003: ID 0a5c:5843 Broadcom Corp. BCM58200 ControlVault 3 (FingerPrint sensor + Contacted SmartCard)
```

However, running `fprintd-enroll` or checking KDE/GNOME Settings results in:
```text
Impossible to enroll: GDBus.Error:net.reactivated.Fprint.Error.NoSuchDevice: No devices available
```

### Why it doesn't work out of the box on Debian:
1. **No Upstream Driver:** Broadcom ControlVault 3 is a secure cryptographic coprocessor that is not supported by standard upstream `libfprint`.
2. **TOD (Touch OEM Driver) Missing:** Canonical maintains a proprietary binary driver (`libfprint-2-tod1-broadcom`) for Ubuntu OEM images, but Debian does not package the TOD extension.
3. **Protocol Bug (`enroll-disconnected`):** The Broadcom Linux driver fails to handle `CV_GET_HOST_TIMESTAMP_INTERRUPT` (interrupt type 5) sent by the chip during enrollment, causing a bulk read timeout and immediate disconnection.
4. **USB Autosuspend:** Linux power management automatically puts the USB reader into autosuspend after 2 seconds, severing communication between finger touches.
5. **Debian Multiarch Incompatibility (`Device status = -99`):** Community shims developed for Fedora hardcode paths to `/usr/lib/libfprint-2/`, which fails on Debian's multiarch layout (`/usr/lib/x86_64-linux-gnu/`).

---

## What This Repository Does

The included [`install.sh`](install.sh) automates the entire solution in a single command:

1. **Builds `libfprint` with TOD support** matching the exact version installed on your Debian system (e.g. `1.94.9+tod1`).
2. **Downloads and installs the official Canonical/Dell Broadcom driver and firmware** (`bcmCitadel`).
3. **Compiles a Debian-compatible Host-Event Shim** ([`src/bcm_cv_host_event_shim.c`](src/bcm_cv_host_event_shim.c)) that intercepts and answers the `CV_GET_HOST_TIMESTAMP_INTERRUPT`, preventing `enroll-disconnected` and `-99` errors.
4. **Disables USB Autosuspend** specifically for `0a5c:5843` so the reader remains powered during scans.
5. **Configures `fprintd` systemd service** with `--no-timeout` and `LD_PRELOAD` for the shim.
6. **Protects your build** with `apt-mark hold libfprint-2-2` so regular `apt upgrade` won't overwrite it.
7. **Enables PAM** (`pam-auth-update`) so fingerprints work for `sudo`, screen locking, and login.

---

## Quick Start

### 1. Clone & Run the Installer

Open a terminal and run:

```bash
git clone https://github.com/tahal15/dell-cv3-fingerprint-debian.git
cd dell-cv3-fingerprint-debian
sudo ./install.sh
```

The script will automatically detect your hardware, install dependencies, compile the necessary components, and initialize the sensor.

---

### 2. Perform a Cold Reboot (Crucial First Step!)

> [!IMPORTANT]
> On its first run, the installer updates the ControlVault chip firmware (e.g., from AAI 5.9.x to 5.15.21). Because ControlVault is an independent security coprocessor, **a standard warm reboot does not power cycle the chip**.
>
> 1. **Shut down the laptop** (`Power off` / `systemctl poweroff`).
> 2. **Wait 15–20 seconds** to let the capacitors discharge.
> 3. **Power on** the laptop.

---

### 3. Enroll Your Fingerprint(s)

Once booted, open a terminal and enroll your right index finger:

```bash
fprintd-enroll -f right-index-finger
```

* Touch the sensor with the flat pad of your finger at a steady pace (not too fast).
* Touch it 8–10 times until you see `Enroll result: enroll-completed`.

*(Recommended: Enroll a backup finger as well, e.g. `fprintd-enroll -f left-index-finger`).*

---

### 4. Test Verification & `sudo`

Verify your enrolled print:
```bash
fprintd-verify
```
*(Should print `verify-match (done)`).*

Test with `sudo`:
```bash
sudo -k && sudo true
```
The terminal will prompt you to place your finger on the sensor instead of asking for your password!

---

## Troubleshooting

### `Device reported an error during enroll: Device disabled to prevent overheating`
The sensor has built-in thermal protection. If you repeatedly tap or swipe too fast, it temporarily disables itself. Let the laptop sit idle for 1–2 minutes, then scan with calm, 1-second pauses between touches.

### `Verify result: verify-unknown-error (done)`
* In the system journal, this corresponds to `identify returned status=137 (0x89), result=0`, which means **no match** (the finger was placed at an angle different from enrollment or pressed too lightly).
* Ensure you perform a **Cold Reboot** after the initial firmware update.
* If needed, delete the old template and re-enroll covering the center and edges:
  ```bash
  fprintd-delete "$USER"
  fprintd-enroll -f right-index-finger
  ```

### Checking Service Logs
To view live logs from the fingerprint daemon:
```bash
sudo journalctl -u fprintd -f
```

---

## Uninstallation / Rollback

To completely revert your system back to stock Debian packages:

```bash
sudo ./uninstall.sh
```

This unholds `libfprint-2-2`, reinstalls the official Debian package, removes all custom libraries, shims, udev rules, and disables PAM integration.

---

## Acknowledgments & References

* **Fran Quinto:** For the excellent tutorial on [Getting Broadcom ControlVault 3 working on Debian 13](https://dev.to/fquinto/getting-the-broadcom-controlvault-3-fingerprint-reader-0a5c5843-working-on-debian-13-44ld).
* **Andrei Grec (GrecAndrei):** For reverse-engineering the `CV_GET_HOST_TIMESTAMP_INTERRUPT` and creating the [ControlVault Fingerprint Passkey Toolkit](https://github.com/GrecAndrei/controlvault-fingerprint-passkey-toolkit).
* **Marco Trevisan (3v1n0):** For maintaining the [libfprint TOD fork](https://gitlab.freedesktop.org/3v1n0/libfprint).
* **Canonical & Dell:** For the [libfprint-2-tod1-broadcom](https://launchpad.net/ubuntu/+source/libfprint-2-tod1-broadcom) proprietary driver package.

---

## License

This project is licensed under the **GNU General Public License v3.0** (GPL-3.0) — see the [LICENSE](LICENSE) file for details.
