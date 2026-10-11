#!/usr/bin/env bash
#
# Broadcom ControlVault 3 (0a5c:5843) Fingerprint Reader Setup for Debian
# Compatible with Dell Latitude (5xxx/7xxx) & Precision laptops
#
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_USER="${SUDO_USER:-$USER}"

echo -e "${BLUE}${BOLD}================================================================${NC}"
echo -e "${BLUE}${BOLD}  Broadcom ControlVault 3 Fingerprint Reader Installer for Debian${NC}"
echo -e "${BLUE}${BOLD}================================================================${NC}"

# 1. Root check
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This installer must be run as root (using sudo).${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

# 2. Hardware check
echo -e "\n${BLUE}[1/8] Checking hardware...${NC}"
if lsusb | grep -q "0a5c:5843"; then
    echo -e "${GREEN}✓ Found Broadcom BCM58200 ControlVault 3 (USB ID 0a5c:5843).${NC}"
else
    echo -e "${YELLOW}Warning: USB device 0a5c:5843 was not found in lsusb.${NC}"
    echo -e "${YELLOW}Please verify that Fingerprint Reader is enabled in BIOS/UEFI.${NC}"
    read -rp "Do you want to continue anyway? [y/N] " confirm
    if [[ ! "$confirm" =~ ^[yY]$ ]]; then
        exit 1
    fi
fi

# 3. Enable deb-src if needed
echo -e "\n${BLUE}[2/8] Checking APT source repositories (deb-src)...${NC}"
HAS_SRC=$(grep -rh -E '^(deb-src|Types:.*deb-src)' /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null || true)
if [ -z "$HAS_SRC" ]; then
    echo "Enabling deb-src repositories..."
    if [ -f /etc/apt/sources.list.d/debian.sources ]; then
        sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
    elif [ -f /etc/apt/sources.list ]; then
        sed -i 's/^# *deb-src/deb-src/' /etc/apt/sources.list
    fi
    apt-get update
else
    echo -e "${GREEN}✓ deb-src repositories are enabled.${NC}"
fi

# 4. Determine libfprint version
echo -e "\n${BLUE}[3/8] Checking installed libfprint version...${NC}"
INSTALLED_VER=$(dpkg-query -W -f='${Version}' libfprint-2-2 2>/dev/null || echo "")
if [ -z "$INSTALLED_VER" ]; then
    echo "libfprint-2-2 not found. Installing stock packages first..."
    apt-get update
    apt-get install -y libfprint-2-2 fprintd libpam-fprintd
    INSTALLED_VER=$(dpkg-query -W -f='${Version}' libfprint-2-2)
fi

UPSTREAM_VER=$(echo "$INSTALLED_VER" | sed -E 's/^[0-9]+://; s/-[0-9]+(\.[0-9]+)*.*$//')
echo -e "Installed package: ${GREEN}${INSTALLED_VER}${NC} (Upstream base: ${GREEN}${UPSTREAM_VER}${NC})"

# 5. Install build dependencies
echo -e "\n${BLUE}[4/8] Installing build tools and dependencies...${NC}"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get build-dep -y libfprint-2-2
apt-get install -y git meson ninja-build build-essential libusb-1.0-0-dev

WORK_DIR=$(mktemp -d /tmp/bcm-cv3-build-XXXXXX)
cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

# 6. Build and install host-event shim
echo -e "\n${BLUE}[5/8] Building Debian-compatible host-event shim...${NC}"
(
    cd "$SCRIPT_DIR/src"
    make clean
    make check
    install -o root -g root -m 0755 libbcm-cv-host-event-shim.so /usr/local/lib/libbcm-cv-host-event-shim.so
)
echo -e "${GREEN}✓ Installed /usr/local/lib/libbcm-cv-host-event-shim.so${NC}"

# 7. Download and install proprietary Broadcom TOD driver & firmware
echo -e "\n${BLUE}[6/8] Fetching Canonical proprietary Broadcom TOD driver & firmware...${NC}"
git clone --depth 1 --branch ubuntu/latest https://git.launchpad.net/libfprint-2-tod1-broadcom "$WORK_DIR/broadcom"

echo "Installing udev rules..."
cp "$WORK_DIR/broadcom/lib/udev/rules.d/60-libfprint-2-device-broadcom.rules" /usr/lib/udev/rules.d/

# Add power/control=on rule to prevent USB autosuspend during biometric operations
cat << 'EOF' > /etc/udev/rules.d/70-libfprint-broadcom-powersave.rules
# Disable USB autosuspend for Broadcom ControlVault 3 reader
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="0a5c", ATTRS{idProduct}=="5843", TEST=="power/control", ATTR{power/control}="on"
EOF

echo "Installing Broadcom driver plugin..."
mkdir -p /usr/lib/x86_64-linux-gnu/libfprint-2/tod-1
cp "$WORK_DIR/broadcom/usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so" \
   /usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/

# Symlink for multiarch/Fedora path compatibility
mkdir -p /usr/lib/libfprint-2/tod-1
ln -sf /usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so \
       /usr/lib/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so

echo "Installing ControlVault firmware files..."
mkdir -p /var/lib/fprint
cp -r "$WORK_DIR/broadcom/var/lib/fprint/fw" /var/lib/fprint/

# 8. Build libfprint with TOD support
echo -e "\n${BLUE}[7/8] Compiling libfprint with TOD support (v${UPSTREAM_VER}+tod1)...${NC}"
git clone --branch "v${UPSTREAM_VER}+tod1" --depth 1 https://gitlab.freedesktop.org/3v1n0/libfprint.git "$WORK_DIR/libfprint"
cd "$WORK_DIR/libfprint"

# Disable tests & examples for cleaner and faster build
sed -i -e "/subdir('tests')/s/^/#/" -e "/subdir('examples')/s/^/#/" meson.build
meson setup build --prefix=/usr -Ddoc=false
meson compile -C build

echo "Backing up stock Debian library..."
if [ ! -f /root/libfprint-2.so.2.debian-orig ]; then
    cp /usr/lib/x86_64-linux-gnu/libfprint-2.so.2 /root/libfprint-2.so.2.debian-orig
fi

echo "Installing TOD-enabled libfprint..."
cp build/libfprint/tod/libfprint-2-tod.so.1 /usr/lib/x86_64-linux-gnu/
ln -sf /usr/lib/x86_64-linux-gnu/libfprint-2-tod.so.1 /usr/lib/x86_64-linux-gnu/libfprint-2-tod.so
cp build/libfprint/libfprint-2.so.2 /usr/lib/x86_64-linux-gnu/
ldconfig

# Prevent apt upgrade from overwriting our TOD library
apt-mark hold libfprint-2-2

# 9. Configure fprintd service and PAM
echo -e "\n${BLUE}[8/8] Configuring fprintd service and PAM...${NC}"
mkdir -p /etc/systemd/system/fprintd.service.d
cat << 'EOF' > /etc/systemd/system/fprintd.service.d/override.conf
[Service]
ExecStart=
ExecStart=/usr/libexec/fprintd --no-timeout
Environment=LD_PRELOAD=/usr/local/lib/libbcm-cv-host-event-shim.so
EOF

# Grant user journal-reading permissions
if [ -n "$TARGET_USER" ] && id "$TARGET_USER" &>/dev/null; then
    usermod -aG systemd-journal "$TARGET_USER" || true
fi

# Reload udev and apply powersave rule immediately
udevadm control --reload-rules
udevadm trigger
for dev in /sys/bus/usb/devices/*; do
    if [ -f "$dev/idVendor" ] && [ -f "$dev/idProduct" ]; then
        if [ "$(cat "$dev/idVendor" 2>/dev/null)" = "0a5c" ] && [ "$(cat "$dev/idProduct" 2>/dev/null)" = "5843" ]; then
            if [ -f "$dev/power/control" ]; then
                echo on > "$dev/power/control" 2>/dev/null || true
            fi
        fi
    fi
done

systemctl daemon-reload
echo "Restarting fprintd (initializing firmware, waiting 15s)..."
systemctl restart fprintd
sleep 15
systemctl restart fprintd
sleep 3

# Enable fingerprint authentication in PAM
pam-auth-update --enable fprintd

echo -e "\n${GREEN}${BOLD}================================================================${NC}"
echo -e "${GREEN}${BOLD}  Installation Complete!${NC}"
echo -e "${GREEN}${BOLD}================================================================${NC}\n"

echo -e "Device detection status:"
fprintd-list "$TARGET_USER" || true

echo -e "\n${YELLOW}${BOLD}IMPORTANT NEXT STEPS:${NC}"
echo -e "1. ${BOLD}Perform a Cold Reboot (Recommended if this is your first install):${NC}"
echo "   Because the installer checks/flashes the ControlVault firmware,"
echo "   perform a full power off (Shut down -> wait 15 seconds -> Power on)."
echo "   A warm reboot does not power cycle the hardware security chip."
echo ""
echo -e "2. ${BOLD}Enroll your fingerprint(s):${NC}"
echo "   fprintd-enroll -f right-index-finger"
echo "   fprintd-enroll -f left-index-finger"
echo ""
echo -e "3. ${BOLD}Verify your fingerprint:${NC}"
echo "   fprintd-verify"
echo ""
echo -e "4. ${BOLD}Test with sudo:${NC}"
echo "   sudo -k && sudo true"
echo ""
