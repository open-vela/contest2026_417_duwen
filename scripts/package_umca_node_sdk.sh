#!/usr/bin/env bash

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
if [ "$#" -ne 0 ]; then
  echo "Usage: $0" >&2
  exit 2
fi
PACKAGE_VERSION=$(sed -n '1p' "$REPO_DIR/packaging/umca-node-sdk/VERSION")
PACKAGE_NAME="umca-node-sdk-${PACKAGE_VERSION}"
DIST_DIR="$REPO_DIR/dist"
TARBALL="$DIST_DIR/${PACKAGE_NAME}.tar.gz"
ZIPFILE="$DIST_DIR/${PACKAGE_NAME}.zip"
CHECKSUM_FILE="$DIST_DIR/SHA256SUMS"

case "$PACKAGE_VERSION" in
  *[!A-Za-z0-9._-]*|'')
    echo "Invalid package version: $PACKAGE_VERSION" >&2
    exit 2
    ;;
esac

if [ -e "$TARBALL" ] || [ -e "$ZIPFILE" ]; then
  echo "Package already exists; refusing to overwrite:" >&2
  echo "  $TARBALL" >&2
  echo "  $ZIPFILE" >&2
  exit 2
fi

STAGE_ROOT=$(mktemp -d)
trap 'rm -rf -- "$STAGE_ROOT"' EXIT HUP INT TERM
PACKAGE_ROOT="$STAGE_ROOT/$PACKAGE_NAME"

install -d "$PACKAGE_ROOT" "$PACKAGE_ROOT/docs" "$PACKAGE_ROOT/demo"
install -d "$PACKAGE_ROOT/tests/unit" "$PACKAGE_ROOT/tests/integration"
install -d "$PACKAGE_ROOT/tests/vectors" "$PACKAGE_ROOT/tools/windows"

install -m 0644 "$REPO_DIR/packaging/umca-node-sdk/CMakeLists.txt" \
  "$PACKAGE_ROOT/CMakeLists.txt"
install -m 0644 "$REPO_DIR/packaging/umca-node-sdk/README.md" \
  "$PACKAGE_ROOT/README.md"
install -m 0644 "$REPO_DIR/packaging/umca-node-sdk/VERSION" \
  "$PACKAGE_ROOT/VERSION"
install -m 0644 "$REPO_DIR/packaging/umca-node-sdk/NOTICE" \
  "$PACKAGE_ROOT/NOTICE"

cp -R "$REPO_DIR/umca/include" "$PACKAGE_ROOT/include"
cp -R "$REPO_DIR/umca/src" "$PACKAGE_ROOT/src"
cp -R "$REPO_DIR/umca/phy" "$PACKAGE_ROOT/phy"
install -d "$PACKAGE_ROOT/ports"
cp -R "$REPO_DIR/umca/ports/posix" "$PACKAGE_ROOT/ports/posix"
install -m 0644 "$REPO_DIR/app/umca_demo/demo_codec.c" \
  "$REPO_DIR/app/umca_demo/demo_topics.h" "$PACKAGE_ROOT/demo/"

install -m 0644 "$REPO_DIR/umca/tests/unit/test_umca.c" \
  "$REPO_DIR/umca/tests/unit/test_demo_codec.c" \
  "$REPO_DIR/umca/tests/unit/test_uart_phy.c" "$PACKAGE_ROOT/tests/unit/"
install -m 0644 "$REPO_DIR/umca/tests/integration/test_three_nodes.c" \
  "$PACKAGE_ROOT/tests/integration/"
install -m 0644 "$REPO_DIR/umca/tests/vectors/README.md" \
  "$REPO_DIR/umca/tests/vectors/umca_vectors.h" \
  "$PACKAGE_ROOT/tests/vectors/"

for document in \
  UMCA_Architecture_Design_v0.3.3.md \
  UMCA_Protocol_Specification_v0.1.2.md \
  UMCA_Software_Design_v0.1.2.md \
  UMCA_Node_Integration_Guide.md \
  UMCA_Portable_Node_Development_Guide.md \
  protocol_conformance.md
do
  install -m 0644 "$REPO_DIR/docs/$document" "$PACKAGE_ROOT/docs/"
done

for tool in "$REPO_DIR"/scripts/windows/*.py \
            "$REPO_DIR"/scripts/windows/requirements.txt
do
  install -m 0644 "$tool" "$PACKAGE_ROOT/tools/windows/"
done

(
  cd "$PACKAGE_ROOT"
  find . -type f ! -name MANIFEST.sha256 -print0 | LC_ALL=C sort -z | \
    xargs -0 sha256sum > MANIFEST.sha256
)

install -d "$DIST_DIR"
(
  cd "$STAGE_ROOT"
  tar -czf "$TARBALL" "$PACKAGE_NAME"
  zip -q -r "$ZIPFILE" "$PACKAGE_NAME"
)

(
  cd "$DIST_DIR"
  sha256sum "$(basename "$TARBALL")" "$(basename "$ZIPFILE")" > \
    "$(basename "$CHECKSUM_FILE")"
  cat "$(basename "$CHECKSUM_FILE")"
)
