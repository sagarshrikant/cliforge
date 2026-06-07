#!/usr/bin/env bash
# =============================================================================
# build-deb.sh — build the cliforge .deb from a CMake staging install.
#
# Single source of truth for Debian packaging: both the GitHub Release workflow
# (release.yml) and the apt-repo workflow (apt-repo.yml) call this so the two
# can never drift.
#
# Usage:
#   build-deb.sh <version> <staging_root> <license_file> <out_dir>
#
#   <version>       e.g. 0.3.0   (no leading "v")
#   <staging_root>  directory that contains usr/...  (from
#                   `cmake --install <build> --prefix <staging_root>/usr`)
#   <license_file>  path to LICENSE (becomes the package copyright)
#   <out_dir>       directory the finished .deb is written into
#
# Prints the path of the built .deb on stdout (last line).
# =============================================================================
set -euo pipefail

VERSION="${1:?version required}"
STAGING="${2:?staging root required}"
LICENSE_FILE="${3:?license file required}"
OUT_DIR="${4:?output dir required}"

ARCH="amd64"
DEB="cliforge_${VERSION}_${ARCH}.deb"
PKG="$(mktemp -d)"

# ---- data tree: copy the staged install verbatim (binary + SDK headers) ----
mkdir -p "${PKG}/usr"
cp -a "${STAGING}/usr/." "${PKG}/usr/"

# ---- docs: copyright + Debian changelog ----
mkdir -p "${PKG}/usr/share/doc/cliforge"
cp "${LICENSE_FILE}" "${PKG}/usr/share/doc/cliforge/copyright"
cat > "${PKG}/usr/share/doc/cliforge/changelog.Debian" <<CL
cliforge (${VERSION}) stable; urgency=medium

  * See https://github.com/shrikant-sagar/cliforge/blob/main/CHANGELOG.md

 -- Shrikant Sagar <shrikant.sagar@gmail.com>  $(date -R)
CL
gzip -9n "${PKG}/usr/share/doc/cliforge/changelog.Debian"

# ---- strip the binary to keep the package small (ignore if already stripped) ----
strip --strip-unneeded "${PKG}/usr/bin/cliforge" 2>/dev/null || true

# ---- normalise permissions (dirs 755, files 644, binary 755) ----
find "${PKG}/usr" -type d -exec chmod 755 {} +
find "${PKG}/usr" -type f -exec chmod 644 {} +
chmod 755 "${PKG}/usr/bin/cliforge"

# ---- control metadata ----
mkdir -p "${PKG}/DEBIAN"
INSTALLED_KB="$(du -sk "${PKG}/usr" | cut -f1)"
cat > "${PKG}/DEBIAN/control" <<CTRL
Package: cliforge
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.17)
Installed-Size: ${INSTALLED_KB}
Maintainer: Shrikant Sagar <shrikant.sagar@gmail.com>
Homepage: https://github.com/shrikant-sagar/cliforge
Description: CLI parser-generator for C89/C99/C11 projects
 cliforge generates cmdline.c, cmdline.h, and cmdline.md from
 declarative .cf schema files. Designed for embedded and systems
 software with MISRA-C compatible output and static storage only.
CTRL

# ---- md5sums over the data files ----
( cd "${PKG}" && find usr -type f -exec md5sum {} \; > DEBIAN/md5sums )

# ---- build ----
mkdir -p "${OUT_DIR}"
fakeroot dpkg-deb --build "${PKG}" "${OUT_DIR}/${DEB}" >&2
rm -rf "${PKG}"
echo "${OUT_DIR}/${DEB}"
