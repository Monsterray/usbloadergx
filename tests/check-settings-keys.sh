#!/bin/sh
# Regression guard for the settings files' keys.
#
# CSettings::Save() and CSettings::SetSetting() each spell every key by hand. A key
# that Save() writes and SetSetting() does not know is lost on the next start; one
# SetSetting() knows and Save() never writes can never be changed; a key spelled twice
# in the SetSetting() chain is dead code behind its first copy. The same holds for
# CGameSettings (one "key:value;" per setting in the game's line).
#
# Usage: tests/check-settings-keys.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# body <file> <function header regex>: the function, from its header to the closing brace.
body() {
	awk -v re="$2" '$0 ~ re { on = 1 } on { print } on && /^}/ { exit }' "$1"
}

# check <label> <file> <save header> <set header> <save key regex> <set key regex> [skip]
# The key regexes print the bare key names, one per line.
check() {
	label="$1"; file="$2"
	body "$file" "$3" > "$TMP/save.txt"
	body "$file" "$4" > "$TMP/set.txt"
	[ -s "$TMP/save.txt" ] || { fail "$label: Save() not found in $file"; return; }
	[ -s "$TMP/set.txt" ] || { fail "$label: SetSetting() not found in $file"; return; }

	# $7: a name Save() writes that is not a setting (the game line's "game:<id> = ").
	sed -nE "$5" "$TMP/save.txt" | grep -vx "${7:-^}" > "$TMP/save.keys"
	sed -nE "$6" "$TMP/set.txt" > "$TMP/set.keys"

	[ -s "$TMP/save.keys" ] || fail "$label: no keys found in Save()"
	[ -s "$TMP/set.keys" ] || fail "$label: no keys found in SetSetting()"

	for list in save set; do
		dup="$(sort "$TMP/$list.keys" | uniq -d)"
		[ -z "$dup" ] || fail "$label: $list key(s) appear twice: $(echo $dup)"
	done

	sort -u "$TMP/save.keys" > "$TMP/save.sorted"
	sort -u "$TMP/set.keys" > "$TMP/set.sorted"
	only_save="$(comm -23 "$TMP/save.sorted" "$TMP/set.sorted")"
	only_set="$(comm -13 "$TMP/save.sorted" "$TMP/set.sorted")"
	[ -z "$only_save" ] || fail "$label: Save() writes but SetSetting() does not read: $(echo $only_save)"
	[ -z "$only_set" ] || fail "$label: SetSetting() reads but Save() never writes: $(echo $only_set)"
}

check "CSettings" "$SRC/settings/CSettings.cpp" \
	'^bool CSettings::Save[(]' '^bool CSettings::SetSetting[(]' \
	's/^[[:space:]]*fprintf\(file, "([A-Za-z0-9_]+) = .*/\1/p' \
	's/^[[:space:]]*(else )?if[[:space:]]*\(strcmp\(name, "([A-Za-z0-9_]+)"\) == 0\).*/\2/p'

# The game line also holds "game:<id> = " (not a setting) and the "# ..." comments.
check "CGameSettings" "$SRC/settings/CGameSettings.cpp" \
	'^bool CGameSettings::Save[(]' '^bool CGameSettings::SetSetting[(]' \
	's/^[[:space:]]*fprintf\(f, "([A-Za-z0-9_]+):%.*/\1/p' \
	's/^[[:space:]]*(else )?if[[:space:]]*\(strcmp\(name, "([A-Za-z0-9_]+)"\) == 0\).*/\2/p' \
	game

[ "$status" -eq 0 ] && echo "OK: Save() and SetSetting() agree on every settings key"
exit "$status"
