#!/bin/sh
set -eu

install_dir="${MINIFY_INSTALL_DIR:-$HOME/.local/bin}"
target="$install_dir/minify"

if [ -e "$target" ] || [ -L "$target" ]; then
    rm -f -- "$target"
    printf 'Removed Minify++ from %s\n' "$target"
else
    printf 'Minify++ is not installed at %s\n' "$target"
fi

printf 'Shell PATH configuration and user files were left unchanged.\n'
