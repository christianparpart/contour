#!/bin/sh
# Generates the reference pages that come out of the contour binary and metainfo.xml.
# Usage: website/tools/generate-reference.sh <contour-binary>
# Run from the repository root. All outputs are git-ignored.
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 <contour-binary>" >&2
    exit 2
fi

contour=$1

mkdir -p docs/vt-sequence
"$contour" documentation configuration global > docs/configuration/index.md
"$contour" documentation configuration profile > docs/configuration/profiles.md
"$contour" documentation vt > docs/vt-sequence/index.md
"$contour" documentation keys > docs/configuration/key-mapping-actions.md
python3 scripts/extract-changelog-from-metainfo.py metainfo.xml docs/release-notes.md
