# SPDX-License-Identifier: MIT
# Maintainer: Rinat Muhamedgaliev <rinat.muhamedgaliev@gmail.com>
# Local build:  git archive --format=tar.gz -o flykooler-$pkgver.tar.gz \
#                   --prefix=flykooler-$pkgver/ HEAD
#               makepkg -si
# For a published release point source at the tag tarball and fill in
# sha256sums.

pkgname=flykooler
pkgver=0.1.1
pkgrel=1
pkgdesc="Fan and lighting control for Flydigi BS laptop coolers (KDE Plasma / Qt 6)"
arch=(x86_64 aarch64)
license=(MIT)
depends=(qt6-base)
makedepends=(cmake gcc)
source=("$pkgname-$pkgver.tar.gz")
sha256sums=(SKIP)

build() {
    cmake -B build -S "$pkgname-$pkgver" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

check() {
    cd build && ctest --output-on-failure
}

package() {
    DESTDIR="$pkgdir" cmake --install build

    # distro integration: udev rule and the systemd user unit
    install -Dm644 "$pkgname-$pkgver/data/70-flydigi-cooler.rules" \
        "$pkgdir/usr/lib/udev/rules.d/70-flydigi-cooler.rules"
    install -Dm644 "$pkgname-$pkgver/data/flykoolerd.service" \
        "$pkgdir/usr/lib/systemd/user/flykoolerd.service"
    install -Dm644 "$pkgname-$pkgver/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
