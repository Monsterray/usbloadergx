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

# 9. FreeType 2.10 and later (devkitPro's ppc-freetype) rasterize a glyph with a
#    FT_RENDER_POOL_SIZE (16 KiB) buffer on the stack; 2.4, which GX bundled before,
#    kept that pool on the heap. Any thread that sets GuiText renders glyphs, and a
#    16 KiB stack overflowed into the heap: SD mode froze at the free space line.
grep -q 'ThreadCallback, this, NULL, 16384' "$SRC/utils/ThreadedTask.cpp" \
	&& fail "ThreadedTask's thread is too small for FreeType's glyph rasterizer"
grep -q 'ProgressThread, NULL, NULL, 16384' "$SRC/prompts/ProgressWindow.cpp" \
	&& fail "the progress window's thread is too small for FreeType's glyph rasterizer"

# 10. One FreeTypeGX serves every GuiText, and the main thread, ThreadedTask and
#     the progress window's thread measure text while the GUI thread draws. A
#     lookup inserts into std::maps and changes the face's size and glyph slot,
#     so every public call takes the font's lock. drawText() calls getWidth() and
#     getOffset(), so the lock has to be recursive.
grep -q 'LWP_MutexInit(&fontMutex, true)' "$SRC/FreeTypeGX.cpp" \
	|| fail "FreeTypeGX does not create a recursive font lock"
for f in drawText getWidth getCharWidth getHeight getOffset; do
	awk -v f="$f" '
		/^[a-z].*FreeTypeGX::[A-Za-z]+\(/ { infn = ($0 ~ ("FreeTypeGX::" f "\\(")) }
		infn && /FontLock lock\(fontMutex\);/ { found = 1 }
		END { exit !found }' "$SRC/FreeTypeGX.cpp" \
		|| fail "FreeTypeGX::$f() reads the glyph cache without the font lock"
done

[ "$status" -eq 0 ] && echo "OK: GUI bounds guards are in place"
exit "$status"
