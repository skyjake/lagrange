#!/bin/sh
# LGRSYNC_ALLOW: optional SHA-256 client certificate fingerprints, separated by spaces or commas.
set -e
if [ -n "$LGRSYNC_ALLOW" ]; then
    allow=/tmp/allow.txt
    : > "$allow"
    for fp in $(echo "$LGRSYNC_ALLOW" | tr ',' ' '); do
        fp=$(echo "$fp" | tr -d ':' | tr 'A-F' 'a-f')
        if ! echo "$fp" | grep -qxE '[0-9a-f]{64}'; then
            echo "invalid fingerprint in LGRSYNC_ALLOW: $fp" >&2
            exit 1
        fi
        echo "$fp" >> "$allow"
    done
    set -- "$@" --allow "$allow"
fi
# The first occurrence of an option wins, so user arguments override these.
exec lgrsyncd "$@" --dir /data --port 1966
