#!/usr/bin/env bash
# Install a pinned, checksum-verified verible release (Linux x86_64) into a local dir.
# Used by `make tools` on Fedora and by CI, so both lint SystemVerilog with the same version.
set -euo pipefail

VERSION="v0.0-4298-g085eef05"
SHA256="ca83778c7c2b73272277bd670d1851c8caf973fd3283688aeb0d36d4b3598a90"
DEST="${1:-.tools/verible}"

if [[ -x "${DEST}/bin/verible-verilog-lint" ]] && "${DEST}/bin/verible-verilog-lint" --version | grep -q "${VERSION}"; then
  echo "verible ${VERSION} already installed in ${DEST}"
  exit 0
fi

tarball="verible-${VERSION}-linux-static-x86_64.tar.gz"
url="https://github.com/chipsalliance/verible/releases/download/${VERSION}/${tarball}"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

curl -fsSL -o "${tmp}/${tarball}" "${url}"
echo "${SHA256}  ${tmp}/${tarball}" | sha256sum -c -
mkdir -p "${DEST}"
tar -xzf "${tmp}/${tarball}" -C "${DEST}" --strip-components=1
"${DEST}/bin/verible-verilog-lint" --version | head -1
