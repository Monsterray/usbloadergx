/* gettext.h for settings_test: LoadLanguage() is not under test. */
#ifndef HOST_SHIM_GETTEXT_H
#define HOST_SHIM_GETTEXT_H

static inline bool gettextLoadLanguage(const char *) { return false; }

#endif
