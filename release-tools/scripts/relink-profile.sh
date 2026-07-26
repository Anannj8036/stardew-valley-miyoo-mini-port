#!/bin/sh

PROFILE_FILE="$ROOT/scripts/relink-profile.env"
[ -f "$PROFILE_FILE" ] || {
    echo "error: missing relinker profile: $PROFILE_FILE" >&2
    return 1
}

set -a
# shellcheck source=release-tools/scripts/relink-profile.env
. "$PROFILE_FILE"
set +a

svmm_write_relink_profile() {
    destination=$1
    printf 'profile=%s\n' "$SVMM_RELINK_PROFILE" > "$destination"
    sed -n '/^SVMM_/p' "$PROFILE_FILE" |
        grep -v '^SVMM_RELINK_PROFILE=' >> "$destination"
}
