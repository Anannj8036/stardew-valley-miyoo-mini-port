#!/bin/sh

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 is required"
}

sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        fail 'shasum or sha256sum is required'
    fi
}

copy_tree() {
    source_dir=$1
    target_dir=$2
    mkdir -p "$target_dir"
    (cd "$source_dir" && tar cf - .) | (cd "$target_dir" && tar xf -)
}

hash_tree() {
    source_dir=$1
    work_dir=$(mktemp -d "${TMPDIR:-/tmp}/stardew-miyoo-hash.XXXXXX")
    hashes="$work_dir/files.sha256"
    manifest="$work_dir/manifest.sha256"

    if command -v shasum >/dev/null 2>&1; then
        hash_command='shasum -a 256'
    elif command -v sha256sum >/dev/null 2>&1; then
        hash_command=sha256sum
    else
        rm -rf "$work_dir"
        fail 'shasum or sha256sum is required'
    fi

    (
        cd "$source_dir" || exit 1
        find . -type f \
            ! -name '.DS_Store' \
            ! -name '._*' \
            ! -name 'Thumbs.db' \
            ! -name '.gitkeep' \
            -exec sh -c "$hash_command \"\$@\"" sh {} +
    ) > "$hashes"
    LC_ALL=C sort -k 2 "$hashes" > "$manifest"
    sha256_file "$manifest"
    rm -rf "$work_dir"
}
