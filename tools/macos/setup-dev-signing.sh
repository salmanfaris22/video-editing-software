#!/bin/bash
# One-time setup: a local code-signing identity for development builds, so
# macOS privacy permissions (Screen Recording, Camera, Microphone) survive
# rebuilds.
#
# Why: an ad-hoc signature is identified by a hash of the binary, which
# changes with every build, so macOS treats each build as a new app and the
# permission you granted no longer applies (System Settings still shows the
# switch on). A certificate signature is identified by the bundle id plus the
# certificate, which stays the same across builds.
#
# The self-signed certificate lives in its own keychain file (added to the
# user keychain search list so codesign finds it), not in the login keychain,
# and is trusted by nothing; it only gives builds a stable identity.
# Remove it (and its search-list entry) with:
#   security delete-keychain ~/Library/Keychains/lectern-dev-signing.keychain-db
set -euo pipefail

NAME="${LECTERN_SIGNING_NAME:-Lectern Local Development}"
KEYCHAIN="$HOME/Library/Keychains/lectern-dev-signing.keychain-db"
PASSWORD="lectern-dev-signing"  # guards only this local, self-signed development key

add_to_search_list() {  # codesign only finds identities in searched keychains
    local current
    current=$(security list-keychains -d user | sed -e 's/^[[:space:]]*"//' -e 's/"$//')
    if ! grep -qxF "$KEYCHAIN" <<<"$current"; then
        # shellcheck disable=SC2086
        security list-keychains -d user -s $current "$KEYCHAIN"
    fi
}

if [ -f "$KEYCHAIN" ]; then
    security unlock-keychain -p "$PASSWORD" "$KEYCHAIN"
    add_to_search_list
    if security find-certificate -c "$NAME" "$KEYCHAIN" >/dev/null 2>&1; then
        echo "Signing identity '$NAME' already set up in $KEYCHAIN"
        exit 0
    fi
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/cert.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
CNF
# macOS's own LibreSSL: its PKCS#12 files import cleanly with `security`.
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -config "$tmp/cert.cnf" \
    -keyout "$tmp/key.pem" -out "$tmp/cert.pem" 2>/dev/null
/usr/bin/openssl pkcs12 -export -inkey "$tmp/key.pem" -in "$tmp/cert.pem" -name "$NAME" \
    -out "$tmp/identity.p12" -passout pass:lectern

[ -f "$KEYCHAIN" ] || security create-keychain -p "$PASSWORD" "$KEYCHAIN"
security set-keychain-settings "$KEYCHAIN"  # never auto-lock
security unlock-keychain -p "$PASSWORD" "$KEYCHAIN"
security import "$tmp/identity.p12" -k "$KEYCHAIN" -P lectern -T /usr/bin/codesign >/dev/null
# Lets codesign use the key without a keychain prompt.
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PASSWORD" "$KEYCHAIN" >/dev/null
add_to_search_list
echo "Created signing identity '$NAME' in $KEYCHAIN"
