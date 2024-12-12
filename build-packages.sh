#!/usr/bin/env bash
set -euo pipefail

PKGVER="1.6.2.patch1"
PKGREL="3"
PKGDESC="A generic USB Chip/Smart Card Interface Devices driver (with TD1000 support)"
URL="https://github.com/bluetech/CCID/tree/td1000"
LICENSES="LGPL,GPL"
ARCH="x86_64"
BUILD_DIR="build"
STAGE_DIR="build/pkg"

FPM="$(ruby -e 'puts Gem.user_dir')/bin/fpm"

if [[ ! -x $FPM ]]; then
    echo 'Install fpm with `gem install --user-install fpm`.'
    exit 1
fi

# Clean previous builds
rm -rf "$BUILD_DIR"

# Build and install with meson inside Podman Ubuntu 22.04 container
podman run --rm \
    --volume "$PWD:/src:Z" \
    --workdir /src \
    ubuntu:22.04 \
    bash -c "
        set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive

        apt-get update
        apt-get install --yes --no-install-recommends \
            meson \
            ninja-build \
            pkg-config \
            build-essential \
            flex \
            libpcsclite-dev \
            libusb-1.0-0-dev \
            zlib1g-dev

        meson setup --prefix=/usr --buildtype=debugoptimized /src/$BUILD_DIR
        meson compile -C /src/$BUILD_DIR
        meson install -C /src/$BUILD_DIR --destdir=/src/$STAGE_DIR
    "

# Move Info.plist and create symlink
mkdir -p "$STAGE_DIR/etc"
mv "$STAGE_DIR/usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist" "$STAGE_DIR/etc/libccid_Info.plist"
ln -sf /etc/libccid_Info.plist "$STAGE_DIR/usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist"

# Install udev rules
install -Dm644 "./src/92_pcscd_ccid.rules" "$STAGE_DIR/usr/lib/udev/rules.d/92_pcscd_ccid.rules"

# Build Debian package
"$FPM" \
  --input-type dir \
  --output-type deb \
  --name libccid \
  --version "$PKGVER" \
  --iteration "$PKGREL" \
  --description "$PKGDESC" \
  --url "$URL" \
  --license "$LICENSES" \
  --architecture "$ARCH" \
  --depends libc6 \
  --depends libusb-1.0-0 \
  --chdir "$STAGE_DIR" \
  .

# Build Arch package
"$FPM" \
  --input-type dir \
  --output-type pacman \
  --name ccid \
  --version "$PKGVER" \
  --iteration "$PKGREL" \
  --description "$PKGDESC" \
  --url "$URL" \
  --license "$LICENSES" \
  --architecture "$ARCH" \
  --depends pcsclite \
  --depends libusb \
  --chdir "$STAGE_DIR" \
  .

echo "Packages built successfully."
