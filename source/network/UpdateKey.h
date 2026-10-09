/****************************************************************************
 * UpdateKey.h
 *
 * Where the updater looks for releases, and the Ed25519 public key their
 * update.txt must be signed with. scripts/update-keygen.sh made the key pair;
 * the private key is the repository secret UPDATE_SIGNING_KEY, which the
 * release workflow signs with. scripts/update-manifest.sh reads the key from
 * this line, so keep its format.
 ***************************************************************************/
#ifndef UPDATE_KEY_H
#define UPDATE_KEY_H

#define UPDATE_PUBLIC_KEY_HEX "9d0afbcd9d6a90d04f2e4054a7e4079b1466c031048975bb199672a7fc73a2e9"
#define UPDATE_RELEASES_URL "https://github.com/Monsterray/usbloadergx/releases"

// A test build (scripts/diag.sh autoinput) can point the updater at a local
// server and a throwaway key. A release build never may.
#if defined(UPDATE_TEST_URL) || defined(UPDATE_TEST_KEY)
#if defined(GITRELEASE) || !defined(AUTOINPUT)
#error UPDATE_TEST_URL and UPDATE_TEST_KEY are for AUTOINPUT test builds only
#endif
#if !defined(UPDATE_TEST_URL) || !defined(UPDATE_TEST_KEY)
#error a test build sets both UPDATE_TEST_URL and UPDATE_TEST_KEY
#endif
#undef UPDATE_RELEASES_URL
#define UPDATE_RELEASES_URL UPDATE_TEST_URL
#undef UPDATE_PUBLIC_KEY_HEX
#define UPDATE_PUBLIC_KEY_HEX UPDATE_TEST_KEY
#endif

#endif
