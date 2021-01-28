pkgname=iptsd
pkgver=0.3.1
pkgrel=1
pkgdesc='Userspace daemon for Intel Precise Touch & Stylus'
arch=('x86_64')
url='https://github.com/linux-surface/iptsd'
license=('GPL')
depends=('libinih')
makedepends=('meson ninja gcc systemd udev')

build() {
	cd $startdir

	arch-meson . build
	meson compile -C build
}

check() {
	cd $startdir

	meson test -C build
}

package() {
	cd $startdir

	DESTDIR="$pkgdir" meson install -C build
}
