#!/bin/sh
# Regression guard for the GUI layer.
#
# Every value checked here comes out of a file the loader does not control: a
# language file, a theme file, a .tpl in a theme folder, wiitdb.xml, or a text
# the user typed. Dolphin renders the GUI, so some of this is reachable there,
# but a bad file is what triggers it, not a normal run.
#
# Usage: tests/check-gui-bounds.sh [path/to/source]
set -u

SRC="${1:-$(dirname "$0")/../source}"
status=0

fail() {
	echo "FAIL: $1" >&2
	status=1
}

# 1. GuiText sizes textDyn by maxWidth, which is a pixel count, and then indexes
#    it by character. A glyph the font does not have measures zero, so the pixel
#    width alone never ends the loop.
grep -q 'DynTextChars' "$SRC/GUI/gui_text.cpp" \
	|| fail "gui_text.cpp does not bound the textDyn buffers by their allocation"
grep -q 'while (text\[i\] && i < maxChars - 1)' "$SRC/GUI/gui_text.cpp" \
	|| fail "MakeDottedText() is not bounded by the buffer size"
grep -q 'text\[ch + 1\] != 0x0000 && i >= 2' "$SRC/GUI/gui_text.cpp" \
	|| fail "WrapText() can still write the dots at a negative index"

# 2. SetTextf fell through to vasprintf with a null format.
grep -q 'if (!format) SetText((char \*) NULL);' "$SRC/GUI/gui_text.cpp" \
	&& fail "GuiText::SetTextf() does not return after a null format"

# 3. A .tpl in a theme folder supplies the texture count and both offsets.
grep -q 'TPLSize < sizeof(TPL_Header)' "$SRC/ImageOperations/TplImage.cpp" \
	|| fail "ParseTplFile() reads the header without checking the file size"
grep -q 'text_header_offset + sizeof(TPL_Texture_Header) > TPLSize' "$SRC/ImageOperations/TplImage.cpp" \
	|| fail "ParseTplFile() does not bound text_header_offset"
grep -q 'GetAvailableSize' "$SRC/GUI/gui_imagedata.cpp" \
	|| fail "LoadTPL() copies the size the header claims, not the size the file holds"

# 4. About fifteen call sites step the category iterator with a list index and
#    never test it, so the iterator itself has to hold at end().
grep -q 'listIter != nameList.end() ? listIter->first' "$SRC/settings/CCategoryList.hpp" \
	|| fail "CCategoryList::getCurrentID() can dereference end()"

# 5. getcwd returns the buffer on success and NULL on failure.
grep -q 'if (getcwd(fulldir, sizeof(fulldir))) return -1;' "$SRC/prompts/filebrowser.cpp" \
	&& fail "ParseDirectory() still treats a working getcwd as a failure"

# 6. A translation comes from a user-editable .lang file and can be far longer
#    than the English source string.
grep -qE '[^n]sprintf\((text|title|msg), ' "$SRC/prompts/PromptWindows.cpp" \
	&& fail "PromptWindows.cpp writes a translated string with an unbounded sprintf"
grep -q 'char errortxt\[50\];' "$SRC/menu/menu_install.cpp" \
	&& fail "menu_install.cpp still builds a translated message in a 50 byte buffer"

# 7. The publish month comes out of wiitdb.xml and indexes a table of twelve.
grep -q 'month >= 1 && month <= 12' "$SRC/prompts/gameinfo.cpp" \
	|| fail "gameinfo.cpp does not bound the publish month before indexing readableMonths"

# 8. Three cases of expand_escape() wrote a character without advancing the
#    write pointer, so the next copied character overwrote it.
grep -q "\*rp = ch;" "$SRC/language/gettext.c" \
	&& fail "expand_escape() drops an octal escape"

[ "$status" -eq 0 ] && echo "OK: GUI bounds guards are in place"
exit "$status"
