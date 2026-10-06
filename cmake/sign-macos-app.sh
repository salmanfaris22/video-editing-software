#!/bin/bash
# Post-build signing of the macOS app bundle. Uses the stable local identity
# from tools/macos/setup-dev-signing.sh when it exists (privacy permissions
# then survive rebuilds), otherwise an ad-hoc signature.
set -euo pipefail
APP="$1"
NAME="${LECTERN_SIGNING_NAME:-Lectern Local Development}"
KEYCHAIN="$HOME/Library/Keychains/lectern-dev-signing.keychain-db"
if [ -f "$KEYCHAIN" ]; then
    security unlock-keychain -p "lectern-dev-signing" "$KEYCHAIN"
    if codesign --force --sign "$NAME" --keychain "$KEYCHAIN" --timestamp=none "$APP" 2>/dev/null; then
        exit 0
    fi
    echo "warning: signing with '$NAME' failed; using an ad-hoc signature (macOS permissions will not survive rebuilds)" >&2
else
    echo "note: ad-hoc signing $(basename "$APP"). Run tools/macos/setup-dev-signing.sh once so macOS permissions survive rebuilds." >&2
fi
codesign --force --sign - --timestamp=none "$APP"
