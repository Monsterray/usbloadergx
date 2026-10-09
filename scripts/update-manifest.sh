#!/usr/bin/env bash
# Write and sign the update.txt the loader's updater reads from a release.
#
#   usage: scripts/update-manifest.sh <version> <dir> <private-key.pem>
#
# <dir> holds the release files: boot.dol, and any of meta.xml, icon.png and
# usbloadergx.wad (the channel build). Each one present is listed with its
# size and SHA-256, and <dir>/update.txt is written and signed with the key.
# The result is checked against the public key compiled into the loader
# (source/network/UpdateKey.h), so a release signed with the wrong key fails
# here instead of on every Wii. Format: source/network/UpdateManifest.h.
#
# To add a channel WAD to a published release, put it next to the release's
# other files, run this again and upload the WAD and the new update.txt.
set -euo pipefail

[ $# -eq 3 ] || { echo "usage: $0 <version> <dir> <private-key.pem>" >&2; exit 1; }
VERSION="$1"
DIR="$2"
KEY="$3"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KEYHEADER="${UPDATE_KEY_HEADER:-$ROOT/source/network/UpdateKey.h}"

[[ "$VERSION" =~ ^(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})$ ]] ||
	{ echo "version must be MAJOR.MINOR.PATCH, not '$VERSION'" >&2; exit 1; }
[ -f "$DIR/boot.dol" ] || { echo "no $DIR/boot.dol" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

body="$TMP/body"
{
	printf 'usbloadergx-update 1\n'
	printf 'version %s\n' "$VERSION"
	for name in boot.dol meta.xml icon.png usbloadergx.wad; do
		[ -f "$DIR/$name" ] || continue
		size=$(wc -c < "$DIR/$name" | tr -d ' ')
		sha=$(sha256sum "$DIR/$name" | cut -d ' ' -f 1)
		printf 'file %s %s %s\n' "$name" "$size" "$sha"
	done
} > "$body"

openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$body" -out "$TMP/sig"
sig=$(od -An -v -tx1 "$TMP/sig" | tr -d ' \n')
[ ${#sig} -eq 128 ] || { echo "signing failed" >&2; exit 1; }

# Verify with the key the loader carries, not the one that signed.
pub=$(sed -n 's/^#define UPDATE_PUBLIC_KEY_HEX "\([0-9a-f]\{64\}\)".*/\1/p' "$KEYHEADER")
[ ${#pub} -eq 64 ] || { echo "no UPDATE_PUBLIC_KEY_HEX in $KEYHEADER" >&2; exit 1; }
# SubjectPublicKeyInfo for Ed25519 is this 12-byte prefix and the raw key.
printf "$(printf '302a300506032b6570032100%s' "$pub" | sed 's/../\\x&/g')" > "$TMP/pub.der"
openssl pkeyutl -verify -pubin -inkey "$TMP/pub.der" -keyform DER -rawin -in "$body" -sigfile "$TMP/sig" >/dev/null ||
	{ echo "the signature does not verify with the public key in $KEYHEADER" >&2; exit 1; }

{ cat "$body"; printf 'signature %s\n' "$sig"; } > "$DIR/update.txt"
cat "$DIR/update.txt"
