#ifndef _WII_USER_SETTINGS_H_
#define _WII_USER_SETTINGS_H_

/* Enough to compile the project */
#define DEVKITPRO
#define WC_NO_HARDEN
#define NO_OLD_SSL_NAMES
/* libwolfssl.a was built without wolfSSL_writev(), and libogc has no sys/uio.h */
#define NO_WRITEV

#endif /* _WII_USER_SETTINGS_H_ */