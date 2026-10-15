/****************************************************************************
 * CheatTxt: checks on a downloaded geckocodes .txt cheat file.
 *
 * The file comes over a connection with no certificate check, so whoever is
 * in the path chooses every byte. It is written to the card only when it
 * looks like the cheat file of the game that was asked for.
 ***************************************************************************/
#ifndef CHEAT_TXT_H_
#define CHEAT_TXT_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Larger than any cheat file on codes.rc24.xyz (Mario Kart Wii's is 69 KB) */
#define CHEAT_TXT_MAX_SIZE (1024 * 1024)

/* A game ID that may go into a URL and a file name: 4 to 6 letters or digits */
bool CheatTxt_IsSafeId(const char *id);

/* True when the size bytes at data are a cheat file for the game id: text
 * with no NUL byte, no larger than CHEAT_TXT_MAX_SIZE, whose first line
 * (after an optional UTF-8 BOM) is a game ID of 4 to 6 letters or digits
 * that starts with the first four characters of id. data need not be
 * terminated. */
bool CheatTxt_IsValid(const char *data, size_t size, const char *id);

#ifdef __cplusplus
}
#endif

#endif
