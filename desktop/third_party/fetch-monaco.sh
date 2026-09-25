#!/usr/bin/env bash
# Fetches the vendored Monaco Editor build used by MonacoEditor
# (monacoeditor.h/.cpp) and unpacks it to third_party/monaco/vs.
#
# third_party/monaco/ is gitignored -- too large for git (~24MB), like
# windows/mingw64/ (see windows/README.md for that one). Run this once per
# machine that needs to build the app; CMakeLists.txt's post-build step
# copies third_party/monaco/vs next to the built binary alongside the
# tracked bridge page in monaco-host/, same pattern as the MinGW bundling.
#
# Requires npm on PATH. Re-run any time to re-fetch the pinned version below.
set -euo pipefail

MONACO_VERSION="0.56.0"

cd "$(dirname "$0")"
rm -rf monaco
mkdir -p monaco

TMP_TARBALL="$(npm pack "monaco-editor@${MONACO_VERSION}" --silent)"
tar xzf "$TMP_TARBALL"
rm -f "$TMP_TARBALL"

# The npm package's min/ folder only ever contains vs/ (the AMD build) --
# that's the only piece we load (see monaco-host/index.html). esm/, dev/,
# and the docs/typings are the source-map/ESM variants we don't use.
mv package/min/vs monaco/vs
rm -rf package

echo "Monaco ${MONACO_VERSION} vendored to third_party/monaco/vs"
