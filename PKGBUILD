pkgname=ccid
pkgver='1.6.1.patch2'
pkgrel=1
pkgdesc='A generic USB Chip/Smart Card Interface Devices driver (with TD1000 support)'
arch=('x86_64')
url='https://github.com/bluetech/CCID/tree/td1000'
license=('LGPL' 'GPL')
depends=('pcsclite' 'libusb' 'flex')
source=("makepkg-clone::git+file://${HOME}/src/CCID")
options=('debug' '!strip')
sha256sums=('SKIP')

build() {
  meson setup --prefix=/usr --buildtype=debugoptimized --reconfigure build "${srcdir}/makepkg-clone"
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"

  # move the configuration file in /etc and create a symbolic link
  mkdir "${pkgdir}/etc"
  mv "${pkgdir}/usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist" "${pkgdir}/etc/libccid_Info.plist"
  ln -s /etc/libccid_Info.plist "${pkgdir}/usr/lib/pcsc/drivers/ifd-ccid.bundle/Contents/Info.plist"

  install -Dm644 "${srcdir}/makepkg-clone/src/92_pcscd_ccid.rules" "${pkgdir}/usr/lib/udev/rules.d/92_pcscd_ccid.rules"
}
