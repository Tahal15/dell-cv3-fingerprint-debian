#!/usr/bin/env bash
#
# Uninstaller for Broadcom ControlVault 3 custom driver stack on Debian
# Reverts system back to stock Debian packages
#
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (using sudo).${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

echo -e "${BLUE}${BOLD}Reverting system to default stock Debian libfprint...${NC}"

# 1. Disable PAM fingerprint authentication
echo "Disabling fingerprint authentication in PAM..."
pam-auth-update --disable fprintd || true

# 2. Unhold package and reinstall official Debian packages
echo "Reinstalling official Debian libfprint packages..."
apt-mark unhold libfprint-2-2 || true
export DEBIAN_FRONTEND=noninteractive
apt-get install --reinstall -y libfprint-2-2

# 3. Clean up custom files
echo "Cleaning up custom libraries, shims, udev rules and overrides..."
rm -f /usr/lib/x86_64-linux-gnu/libfprint-2-tod.so* \
      /usr/lib/x86_64-linux-gnu/libfprint-2/tod-1/libfprint-2-tod-1-broadcom.so \
      /usr/lib/udev/rules.d/60-libfprint-2-device-broadcom.rules \
      /etc/udev/rules.d/70-libfprint-broadcom-powersave.rules \
      /usr/local/lib/libbcm-cv-host-event-shim.so \
      /etc/systemd/system/fprintd.service.d/override.conf

rm -rf /usr/lib/libfprint-2/tod-1 2>/dev/null || true

# 4. Reload services
ldconfig
udevadm control --reload-rules
udevadm trigger
systemctl daemon-reload
systemctl restart fprintd

echo -e "\n${GREEN}${BOLD}✓ Uninstallation complete. System restored to stock Debian state.${NC}"
