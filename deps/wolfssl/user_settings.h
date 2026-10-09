/* wolfSSL configuration for USB Loader GX. deps/build.sh installs it as
   wolfssl/wolfcrypt/user_settings.h and builds wolfSSL with it; GX compiles
   with -DWOLFSSL_USER_SETTINGS, so both sides see the same settings.

   GX is an HTTPS client only (source/network/https.c): TLS 1.2, SNI, session
   resumption, its own send/receive callbacks, no certificate check. */
#ifndef USBLOADERGX_WOLFSSL_USER_SETTINGS_H
#define USBLOADERGX_WOLFSSL_USER_SETTINGS_H

/* Platform: sockets through libogc's net_send/net_recv (wolfio.h). */
#define DEVKITPRO
#define SINGLE_THREADED            /* as the library GX used before; one TLS user at a time */
#define NO_FILESYSTEM
#define NO_WRITEV                  /* libogc has no sys/uio.h */
#define WORDS_BIGENDIAN            /* configure's AC_C_BIGENDIAN; without it every hash
                                      comes out wrong and wc_InitRng() fails its
                                      self-test (DRBG_CONT_FIPS_E, -209) */
#define SIZEOF_LONG_LONG 8
#define WOLFSSL_GENERAL_ALIGNMENT 4

/* The PowerPC can read no random source on a Wii: libogc offers none, and
   newlib's getentropy() has no backend. GX supplies the seed
   (source/network/seed.c); the library GX used before seeded from rand(). */
#define CUSTOM_RAND_GENERATE_SEED GXGenerateSeed
extern int GXGenerateSeed(unsigned char *output, unsigned int sz);

/* Protocol */
#define NO_OLD_TLS                 /* TLS 1.2 only; GX asks for wolfTLSv1_2_client_method() */
#define NO_OLD_SSL_NAMES
#define HAVE_TLS_EXTENSIONS
#define HAVE_SNI
#define HAVE_SUPPORTED_CURVES
#define HAVE_EXTENDED_MASTER
#define HAVE_ENCRYPT_THEN_MAC
#define HAVE_SESSION_TICKET
#define HAVE_SECURE_RENEGOTIATION

/* Algorithms: what servers offer TLS 1.2 clients today */
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define HAVE_CURVE25519
#define HAVE_ED25519               /* the updater's signed update.txt (network/update.cpp);
                                      needs WOLFSSL_SHA512 */
#define HAVE_FFDHE_2048
#define SP_INT_BITS 4096           /* RSA keys up to 4096 bits: art.gametdb.com uses one.
                                      A 32-bit target defaults to 2048, and the
                                      handshake then fails with WC_KEY_SIZE_E (-234) */
#define WC_RSA_BLINDING
#define TFM_TIMING_RESISTANT
#define HAVE_AESGCM
#define HAVE_CHACHA
#define HAVE_POLY1305
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512

/* Not needed by an HTTPS client */
#define NO_DSA
#define NO_RC4
#define NO_MD4
#define NO_DES3
#define NO_PSK
#define NO_PWDBASED

#endif
