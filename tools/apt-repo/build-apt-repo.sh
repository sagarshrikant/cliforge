#!/usr/bin/env bash
# =============================================================================
# build-apt-repo.sh — assemble/refresh a signed APT repository of static files
# suitable for hosting on GitHub Pages.
#
# It maintains a "pool + dists" layout (no stateful database), so the entire
# repo is just files that can be committed to the gh-pages branch:
#
#   <repo>/pool/main/c/cliforge/cliforge_<ver>_amd64.deb   (all versions accrue)
#   <repo>/dists/stable/main/binary-amd64/Packages[.gz]
#   <repo>/dists/stable/Release  +  InRelease  +  Release.gpg   (GPG-signed)
#   <repo>/cliforge-archive-keyring.gpg   (public key for users)
#   <repo>/index.html                     (human install instructions)
#
# On each release the new .deb is dropped into pool/ alongside existing ones,
# then the metadata is regenerated over the whole pool and re-signed — so older
# versions remain installable and `apt upgrade` sees the new one.
#
# Usage:
#   build-apt-repo.sh <repo_dir> <deb_file> <pages_base_url> [<gpg_key_id>]
#
#   <repo_dir>        gh-pages working tree (may already contain pool/ + dists/)
#   <deb_file>        the freshly built .deb to add
#   <pages_base_url>  e.g. https://shrikant-sagar.github.io/cliforge
#   <gpg_key_id>      optional; signing key (uses the default secret key if omitted)
#
# Requires: apt-ftparchive (apt-utils), gpg, gzip.
# =============================================================================
set -euo pipefail

REPO="${1:?repo dir required}"
DEB="${2:?deb file required}"
BASE_URL="${3:?pages base url required}"
KEY_ID="${4:-}"

SUITE="stable"
COMP="main"
ARCH="amd64"
KEYRING="cliforge-archive-keyring.gpg"

KEYOPT=()
[ -n "${KEY_ID}" ] && KEYOPT=(--local-user "${KEY_ID}")

# ---- 1. place the .deb into the pool ----
POOL="${REPO}/pool/${COMP}/c/cliforge"
mkdir -p "${POOL}"
cp -f "${DEB}" "${POOL}/"

# ---- 2. (re)generate the Packages index over the whole pool ----
BIN_DIR="${REPO}/dists/${SUITE}/${COMP}/binary-${ARCH}"
mkdir -p "${BIN_DIR}"
(
    cd "${REPO}"
    apt-ftparchive --arch "${ARCH}" packages pool > "dists/${SUITE}/${COMP}/binary-${ARCH}/Packages"
    gzip -9kf "dists/${SUITE}/${COMP}/binary-${ARCH}/Packages"
)

# ---- 3. (re)generate the signed Release ----
(
    cd "${REPO}"
    rm -f "dists/${SUITE}/Release" "dists/${SUITE}/Release.gpg" "dists/${SUITE}/InRelease"
    apt-ftparchive \
        -o "APT::FTPArchive::Release::Origin=cliforge" \
        -o "APT::FTPArchive::Release::Label=cliforge" \
        -o "APT::FTPArchive::Release::Suite=${SUITE}" \
        -o "APT::FTPArchive::Release::Codename=${SUITE}" \
        -o "APT::FTPArchive::Release::Components=${COMP}" \
        -o "APT::FTPArchive::Release::Architectures=${ARCH}" \
        -o "APT::FTPArchive::Release::Description=cliforge apt repository" \
        release "dists/${SUITE}" > "dists/${SUITE}/Release"

    # Inline-signed (InRelease) and detached (Release.gpg): apt accepts either.
    gpg --batch --yes --pinentry-mode loopback "${KEYOPT[@]}" \
        --clearsign -o "dists/${SUITE}/InRelease" "dists/${SUITE}/Release"
    gpg --batch --yes --pinentry-mode loopback "${KEYOPT[@]}" \
        -abs -o "dists/${SUITE}/Release.gpg" "dists/${SUITE}/Release"
)

# ---- 4. export the public signing key for users ----
gpg --export ${KEY_ID:+"${KEY_ID}"} > "${REPO}/${KEYRING}"
gpg --armor --export ${KEY_ID:+"${KEY_ID}"} > "${REPO}/cliforge.asc"

# ---- 5. landing page with copy-paste install instructions ----
cat > "${REPO}/index.html" <<HTML
<!doctype html>
<meta charset="utf-8">
<title>cliforge apt repository</title>
<style>
 body{font-family:system-ui,sans-serif;max-width:760px;margin:3rem auto;padding:0 1rem;line-height:1.5}
 pre{background:#f4f4f4;padding:1rem;border-radius:8px;overflow:auto}
 code{font-family:ui-monospace,monospace}
 h1{margin-bottom:0}.sub{color:#666}
</style>
<h1>cliforge apt repository</h1>
<p class="sub">CLI parser-generator for C89/C99/C11 projects.</p>

<h2>Install</h2>
<pre><code># 1. Add the signing key
curl -fsSL ${BASE_URL}/${KEYRING} | sudo tee /usr/share/keyrings/${KEYRING} > /dev/null

# 2. Add the repository
echo "deb [signed-by=/usr/share/keyrings/${KEYRING}] ${BASE_URL} ${SUITE} ${COMP}" \\
  | sudo tee /etc/apt/sources.list.d/cliforge.list

# 3. Install
sudo apt update
sudo apt install cliforge</code></pre>

<p>Updates arrive through normal <code>sudo apt update &amp;&amp; sudo apt upgrade</code>.</p>
<p>Source &amp; releases: <a href="https://github.com/shrikant-sagar/cliforge">github.com/shrikant-sagar/cliforge</a></p>
HTML

echo "apt repo refreshed at: ${REPO}  (added $(basename "${DEB}"))"
