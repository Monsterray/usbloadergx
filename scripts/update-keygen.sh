#!/usr/bin/env bash
# Make the Ed25519 key pair that signs the updater's update.txt.
#
#   usage: scripts/update-keygen.sh <private-key.pem>
#
# Writes the private key (PEM) to the path given, which must be outside the
# repository, and prints the public key as the line source/network/UpdateKey.h
# needs. Then:
#   1. put that line in UpdateKey.h and commit it;
#   2. store the whole PEM file as the repository secret UPDATE_SIGNING_KEY
#      (Settings > Secrets and variables > Actions), which the release workflow
#      signs with;
#   3. keep a backup of the PEM file offline.
# A new key pair locks out every loader built with the old public key: those
# loaders refuse updates signed with the new key and must be updated by hand.
set -euo pipefail

[ $# -eq 1 ] || { echo "usage: $0 <private-key.pem>" >&2; exit 1; }
KEY="$1"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

[ -e "$KEY" ] && { echo "$KEY exists; not overwriting a signing key" >&2; exit 1; }
KEYDIR="$(cd "$(dirname "$KEY")" && pwd)"
case "$KEYDIR/" in
	"$ROOT"/*) echo "keep the private key outside the repository" >&2; exit 1 ;;
esac

umask 077
openssl genpkey -algorithm ed25519 -out "$KEY"
# The DER public key ends with the 32 raw key bytes.
hex=$(openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -v -tx1 | tr -d ' \n')
[ ${#hex} -eq 64 ] || { echo "could not read the public key" >&2; exit 1; }
echo "Private key: $KEY"
echo "For source/network/UpdateKey.h:"
echo "#define UPDATE_PUBLIC_KEY_HEX \"$hex\""
