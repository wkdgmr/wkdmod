#!/usr/bin/env bash
set -euo pipefail
set -x

BUILD_DIR="${1-build}"

mkdir -p "${BUILD_DIR}/package"

PKG_PATH=("${BUILD_DIR}/_CPack_Packages/Linux/7Z/"wkdmod-*/)
PKG_PATH="${PKG_PATH[@]}"
PKG_PATH="${PKG_PATH%/}"

cp "${PKG_PATH}/bin/wkdmod" "${BUILD_DIR}/package/wkdmod"

if [[ -f "${PKG_PATH}/lib/discord_game_sdk.so" ]]; then
	cp "${PKG_PATH}/lib/discord_game_sdk.so" "${BUILD_DIR}/package/"
	cat <<'SH' > "${BUILD_DIR}/package/wkdmod.sh"
#!/bin/sh
BASEDIR="$(dirname "$(realpath "$0")")"
LD_LIBRARY_PATH="$BASEDIR" "$BASEDIR"/wkdmod
SH
	chmod +x "${BUILD_DIR}/package/wkdmod.sh"
fi

cp "${BUILD_DIR}/wkdmod" "${BUILD_DIR}/package/wkdmod"
cp "${BUILD_DIR}/devilutionx.mpq" "${BUILD_DIR}/package/devilutionx.mpq"

if which dpkg 2>/dev/null; then
	cp "${BUILD_DIR}/"wkdmod*.deb "${BUILD_DIR}/package/wkdmod.deb"
fi
if which rpmbuild; then
	cp "${BUILD_DIR}/"wkdmod*.rpm "${BUILD_DIR}/package/wkdmod.rpm"
fi

cp ./Packaging/nix/README.txt "${BUILD_DIR}/package/README.txt"
cp ./Packaging/resources/LICENSE.CC-BY.txt "${BUILD_DIR}/package/LICENSE.CC-BY.txt"
cp ./Packaging/resources/LICENSE.OFL.txt "${BUILD_DIR}/package/LICENSE.OFL.txt"
cd "${BUILD_DIR}/package/" && tar -cavf ../../wkdmod.tar.xz *
