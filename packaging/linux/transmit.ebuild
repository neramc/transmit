# Copyright 2026 Transmit contributors
# Distributed under the terms of the GNU Affero General Public License v3

EAPI=8

inherit cmake

DESCRIPTION="Move a computer's environment to one running another operating system"
HOMEPAGE="https://github.com/neramc/transmit"
SRC_URI="https://github.com/neramc/transmit/archive/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="AGPL-3+"
SLOT="0"
KEYWORDS="~amd64 ~arm64"
IUSE="+crypto +lzma test"
RESTRICT="!test? ( test )"

RDEPEND="
    dev-qt/qtbase:6[gui,network,sql,sqlite]
    dev-qt/qtdeclarative:6
    app-arch/zstd
    dev-db/sqlite:3
    crypto? ( dev-libs/openssl:= )
    lzma? ( app-arch/xz-utils )
    app-crypt/libsecret
"
DEPEND="${RDEPEND}"

src_configure() {
    local mycmakeargs=(
        -DTRANSMIT_WITH_OPENSSL=$(usex crypto)
        -DTRANSMIT_WITH_LZMA=$(usex lzma)
        -DTRANSMIT_BUILD_TESTS=$(usex test)
        -DTRANSMIT_WITH_UPDATER=OFF
    )
    cmake_src_configure
}
