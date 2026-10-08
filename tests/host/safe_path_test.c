/* safe_path_test.c - IsSafeRelativePath() against the names an attacker would send.
 *
 * Runs on the host: source/FileOperations/SafePath.c has no libogc in it, so the
 * code under test is the exact code the Wii runs. It is the only check between a
 * name chosen by someone else (a zip entry from a download or a wiiload sender,
 * the wiiload file name itself) and a write to the user's card.
 */
#include <stdio.h>
#include "FileOperations/SafePath.c"

static int failures;

static void expect(const char *name, bool want)
{
	bool got = IsSafeRelativePath(name);
	if (got != want)
	{
		printf("FAIL: IsSafeRelativePath(%s%s%s) = %s, want %s\n",
		       name ? "\"" : "", name ? name : "NULL", name ? "\"" : "",
		       got ? "true" : "false", want ? "true" : "false");
		failures++;
	}
}

int main(void)
{
	/* Ordinary names inside the destination. */
	expect("boot.dol", true);
	expect("apps/myapp/boot.dol", true);
	expect("a/b/c/d.txt", true);
	expect("dir/", true);

	/* Dots that are part of a name, not a parent reference. */
	expect("..a/b", true);
	expect("a/..b/c", true);
	expect("a..b", true);
	expect("....", true);
	expect(".hidden", true);
	expect("./x", true);

	/* Parent references, with either separator, anywhere in the name. */
	expect("..", false);
	expect("../boot.dol", false);
	expect("a/../../b", false);
	expect("a/..", false);
	expect("..\\boot.dol", false);
	expect("a\\..\\b", false);
	expect("a/b\\..", false);
	expect("../../../apps/usbloader_gx/boot.dol", false);

	/* Absolute paths and device names. */
	expect("/apps/x", false);
	expect("\\apps\\x", false);
	expect("sd:/apps/x", false);
	expect("usb1:/x", false);
	expect("a:b", false);

	/* Nothing at all. */
	expect("", false);
	expect(NULL, false);

	if (failures)
		return 1;
	printf("safe_path_test: all cases pass\n");
	return 0;
}
