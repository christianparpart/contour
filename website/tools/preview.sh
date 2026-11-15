#!/bin/sh
# Previews the website. Usage: website/tools/preview.sh serve|build
# Run from the repository root, after creating out/website-venv.
set -eu

mkdocs=out/website-venv/bin/mkdocs

case "${1:-}" in
serve)
    port=8080
    echo "http://localhost:$port/"
    if [ "$(uname -s)" = Darwin ]; then
        ip=$(ipconfig getifaddr en0 || ipconfig getifaddr en1 || true)
    else
        ip=$(hostname -I 2>/dev/null | cut -d' ' -f1 || true)
    fi
    if [ -n "${ip:-}" ]; then
        echo "http://$ip:$port/"
    fi
    exec "$mkdocs" serve -a "0.0.0.0:$port"
    ;;
build)
    # File-based URLs, so the static copy works on hosts without directory index.
    "$mkdocs" build --no-directory-urls -d out/website-preview
    ;;
*)
    echo "usage: $0 serve|build" >&2
    exit 2
    ;;
esac
