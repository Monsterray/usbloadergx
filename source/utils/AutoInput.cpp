/****************************************************************************
 * AutoInput: scripted controller input for unattended test runs.
 *
 * sd:/autoinput.txt holds one event per line, the time in milliseconds since
 * the first UpdatePads() call first:
 *
 *   # comment
 *   25000 press B            GameCube pad button, held 100 ms
 *   30000 hold RIGHT 1500    held for 1500 ms
 *   32000 point 535 417      Wii Remote pointer on at x y (screen coordinates)
 *   33000 unpoint            pointer off again
 *   34000 stick 70 0 400     GameCube main stick, held 400 ms
 *   40000 download https://example.com/   downloadfile() it now and log the
 *                            size, the time and the start of the body (HTTPS
 *                            and wolfSSL in Dolphin, whose sockets reach the host)
 *   50000 downloadto https://example.com/x.zip sd:/x.zip
 *                            DownloadFileToPath() it: streamed to the card through
 *                            sd:/x.zip.part; logs the result and MEM2 free
 *   33000 memhog 1700        hold MEM2 until only 1700 KB are free, as on a
 *                            Wii running GX (Dolphin has some 45 MB free)
 *   60000 memreport          MEM_Report() now, with the browser and its covers
 *                            still up ("mem autoinput <ms>: ...")
 *   70000 exit hbc           leave GX the way an exit menu does, from the game
 *                            browser's main loop: hbc (Sys_LoadHBC), menu
 *                            (Sys_LoadMenu), loader (Sys_BackToLoader, the return
 *                            stub) or prompt (WindowExitPrompt, GX's HOME menu).
 *                            The HOME overlay reads the controllers itself, so
 *                            scripted presses cannot choose its Exit items.
 *   80000 do source homebrew run in the browser's main loop, for hardware runs
 *                            that cannot see the screen: source wii|gc|nand|
 *                            emunand|homebrew|custom (the list the source button
 *                            shows), list (log every entry: type, id, title),
 *                            boot <ID> (start that entry of the current list as
 *                            quick boot does; homebrew apps as the browser does),
 *                            update nintendont|gametdb|cheats (the Update menu's
 *                            functions; logs their result), download <url> (as
 *                            "download" but in the main thread: on a Wii a slow one
 *                            in the GUI thread trips the hbc agent's 120 s hang check),
 *                            install (the Install button: MenuInstall())
 *   85000 answer 1 10        the next 10 WindowPrompt()s choose their first button
 *                            once shown (the last one if they have fewer); each is
 *                            logged with its title and text. Lets the real flows
 *                            (install, updates) run through their questions.
 *
 * Buttons: A B X Y Z START UP DOWN LEFT RIGHT L R. Everything goes to channel
 * 0, merged with whatever a real controller sends. Each event is logged with
 * gprintf, so the gecko log shows when it fired.
 ***************************************************************************/
#ifdef AUTOINPUT

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <ogc/lwp_watchdog.h>
#include "AutoInput.h"
#include "GUI/gui.h"
#include "network/https.h"
#include "network/update.h"
#include "memory/mem2.h"
#include "gecko.h"

namespace
{
enum Kind { EV_PRESS, EV_POINT, EV_UNPOINT, EV_STICK, EV_DOWNLOAD, EV_DOWNLOADTO, EV_MEMHOG, EV_MEMREPORT, EV_EXIT, EV_DO, EV_ANSWER };

struct Event
{
	u32 ms;
	Kind kind;
	u32 bits;
	u32 duration;
	int x, y;
	std::string url;
	std::string path;
};

std::vector<Event> events;
size_t nextEvent = 0;
bool loaded = false;
u64 start = 0;
u32 lastTry = 0;

u32 heldBits = 0, heldUntil = 0, prevBits = 0;
bool pointing = false;
int pointX = 0, pointY = 0;
int stickX = 0, stickY = 0;
u32 stickUntil = 0;
//! Set here in the GUI thread, taken by the browser's main loop: an exit has to
//! run in the main thread
volatile int pendingExit = AUTOINPUT_EXIT_NONE;
//! The same hand-over for "do" events: filled here, then the flag set; the main
//! thread copies it, then clears the flag. A later "do" waits for the flag.
char pendingAction[128];
volatile bool actionPending = false;
char takenAction[128];
//! "answer": the button the next prompts choose, and how many prompts are left
volatile int answerButton = 0, answersLeft = 0;

u32 ButtonBits(const char *name)
{
	static const struct { const char *name; u32 bits; } map[] = {
		{ "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B }, { "X", PAD_BUTTON_X }, { "Y", PAD_BUTTON_Y },
		{ "Z", PAD_TRIGGER_Z }, { "START", PAD_BUTTON_START }, { "L", PAD_TRIGGER_L }, { "R", PAD_TRIGGER_R },
		{ "UP", PAD_BUTTON_UP }, { "DOWN", PAD_BUTTON_DOWN }, { "LEFT", PAD_BUTTON_LEFT }, { "RIGHT", PAD_BUTTON_RIGHT },
	};
	for (const auto &m : map)
		if (strcasecmp(name, m.name) == 0)
			return m.bits;
	return 0;
}

// The SD card is mounted some seconds after the first UpdatePads(), so this
// is retried until the file opens.
void Load()
{
	FILE *f = fopen("sd:/autoinput.txt", "r");
	if (!f)
		return;

	char line[600], cmd[16], arg[16], url[256], path[256];
	while (fgets(line, sizeof(line), f))
	{
		Event e = {};
		int a = 0, b = 0, c = 0;
		if (line[0] == '#' || sscanf(line, "%u %15s", &e.ms, cmd) != 2)
			continue;

		if (strcasecmp(cmd, "press") == 0 && sscanf(line, "%*u %*s %15s", arg) == 1)
		{
			e.kind = EV_PRESS; e.bits = ButtonBits(arg); e.duration = 100;
		}
		else if (strcasecmp(cmd, "hold") == 0 && sscanf(line, "%*u %*s %15s %d", arg, &a) == 2)
		{
			e.kind = EV_PRESS; e.bits = ButtonBits(arg); e.duration = a;
		}
		else if (strcasecmp(cmd, "point") == 0 && sscanf(line, "%*u %*s %d %d", &a, &b) == 2)
		{
			e.kind = EV_POINT; e.x = a; e.y = b;
		}
		else if (strcasecmp(cmd, "unpoint") == 0)
		{
			e.kind = EV_UNPOINT;
		}
		else if (strcasecmp(cmd, "stick") == 0 && sscanf(line, "%*u %*s %d %d %d", &a, &b, &c) == 3)
		{
			e.kind = EV_STICK; e.x = a; e.y = b; e.duration = c;
		}
		else if (strcasecmp(cmd, "download") == 0 && sscanf(line, "%*u %*s %255s", url) == 1)
		{
			e.kind = EV_DOWNLOAD; e.url = url;
		}
		else if (strcasecmp(cmd, "memhog") == 0 && sscanf(line, "%*u %*s %d", &a) == 1)
		{
			e.kind = EV_MEMHOG; e.x = a;
		}
		else if (strcasecmp(cmd, "memreport") == 0)
		{
			e.kind = EV_MEMREPORT;
		}
		else if (strcasecmp(cmd, "downloadto") == 0 && sscanf(line, "%*u %*s %255s %255s", url, path) == 2)
		{
			e.kind = EV_DOWNLOADTO; e.url = url; e.path = path;
		}
		else if (strcasecmp(cmd, "exit") == 0 && sscanf(line, "%*u %*s %15s", arg) == 1 &&
				 (a = strcasecmp(arg, "hbc") == 0 ? AUTOINPUT_EXIT_HBC
					: strcasecmp(arg, "menu") == 0 ? AUTOINPUT_EXIT_MENU
					: strcasecmp(arg, "loader") == 0 ? AUTOINPUT_EXIT_LOADER
					: strcasecmp(arg, "prompt") == 0 ? AUTOINPUT_EXIT_PROMPT : AUTOINPUT_EXIT_NONE) != AUTOINPUT_EXIT_NONE)
		{
			e.kind = EV_EXIT; e.x = a;
		}
		else if (strcasecmp(cmd, "do") == 0 && sscanf(line, "%*u %*s %255[^\r\n]", url) == 1)
		{
			e.kind = EV_DO; e.url = url;
		}
		else if (strcasecmp(cmd, "answer") == 0 && sscanf(line, "%*u %*s %d %d", &a, &b) == 2 && a >= 1 && b >= 0)
		{
			e.kind = EV_ANSWER; e.x = a; e.y = b;
		}
		else
		{
			gprintf("autoinput: cannot read line: %s", line);
			continue;
		}
		events.push_back(e);
	}
	fclose(f);
	loaded = true;
	gprintf("autoinput: %u events loaded\n", (u32) events.size());
}
}

void AutoInput_Apply(void)
{
	u64 now = gettime();
	if (!start)
		start = now;
	u32 ms = ticks_to_millisecs(diff_ticks(start, now));

	if (!loaded && ms - lastTry >= 500)
	{
		lastTry = ms;
		Load();
	}

	while (nextEvent < events.size() && events[nextEvent].ms <= ms)
	{
		// The main thread has not taken the last "do" yet: a "do" or an "exit" waits,
		// since the browser looks for an exit first
		if ((events[nextEvent].kind == EV_DO || events[nextEvent].kind == EV_EXIT) && actionPending)
			break;
		const Event &e = events[nextEvent++];
		switch (e.kind)
		{
			case EV_PRESS:
				heldBits = e.bits;
				heldUntil = ms + e.duration;
				gprintf("autoinput %u: buttons %04x for %u ms\n", ms, e.bits, e.duration);
				break;
			case EV_POINT:
				pointing = true;
				pointX = e.x;
				pointY = e.y;
				gprintf("autoinput %u: point %d %d\n", ms, e.x, e.y);
				break;
			case EV_UNPOINT:
				pointing = false;
				gprintf("autoinput %u: unpoint\n", ms);
				break;
			case EV_STICK:
				stickX = e.x;
				stickY = e.y;
				stickUntil = ms + e.duration;
				gprintf("autoinput %u: stick %d %d for %u ms\n", ms, e.x, e.y, e.duration);
				break;
			case EV_DOWNLOAD:
			{
				// Blocks this thread until the download ends; fine for a test run.
				struct download file = {};
				u64 begin = gettime();
				downloadfile(e.url.c_str(), &file);
				gprintf("autoinput %u: download %s: %u bytes in %u ms: %.60s\n", ms, e.url.c_str(),
						(u32) file.size, (u32) ticks_to_millisecs(diff_ticks(begin, gettime())),
						file.data ? file.data : "");
				if (file.data)
					MEM2_free(file.data);
				break;
			}
			case EV_MEMHOG:
			{
				// Held until GX exits: the test is what still works with this little
				u32 freeBefore = MEM2_freesize();
				u32 keep = (u32) e.x * 1024, blocks = 0;
				// The heap has more than one region, so this takes several blocks
				for (u32 size = 32 << 20; size >= 16 << 10 && MEM2_freesize() > keep;)
				{
					u32 want = MEM2_freesize() - keep;
					if (MEM2_alloc(want < size ? want : size))
						blocks++;
					else
						size /= 2;
				}
				gprintf("autoinput %u: memhog in %u blocks, MEM2 free %u before, %u after\n", ms, blocks,
						freeBefore, MEM2_freesize());
				break;
			}
			case EV_MEMREPORT:
			{
				char where[32];
				snprintf(where, sizeof(where), "autoinput %u", ms);
				MEM_Report(where);
				break;
			}
			case EV_DOWNLOADTO:
			{
				u32 freeBefore = MEM2_freesize();
				u64 begin = gettime();
				int size = DownloadFileToPath(e.url.c_str(), e.path.c_str(), false);
				gprintf("autoinput %u: downloadto %s -> %s: %d in %u ms, MEM2 free %u before, %u after\n", ms,
						e.url.c_str(), e.path.c_str(), size, (u32) ticks_to_millisecs(diff_ticks(begin, gettime())),
						freeBefore, MEM2_freesize());
				break;
			}
			case EV_EXIT:
				pendingExit = e.x;
				gprintf("autoinput %u: exit %d requested\n", ms, e.x);
				break;
			case EV_DO:
				snprintf(pendingAction, sizeof(pendingAction), "%s", e.url.c_str());
				actionPending = true;
				gprintf("autoinput %u: do %s\n", ms, pendingAction);
				break;
			case EV_ANSWER:
				answerButton = e.x;
				answersLeft = e.y;
				gprintf("autoinput %u: answer button %d on the next %d prompts\n", ms, e.x, e.y);
				break;
		}
	}

	GuiTrigger &t = userInput[0];
	u32 bits = (ms < heldUntil) ? heldBits : 0;
	t.pad.btns_h |= bits;
	t.pad.btns_d |= bits & ~prevBits;
	t.pad.btns_u |= prevBits & ~bits;
	prevBits = bits;

	if (ms < stickUntil)
	{
		t.pad.stickX = stickX;
		t.pad.stickY = stickY;
	}

	if (pointing)
	{
		t.wpad.ir.valid = 1;
		t.wpad.ir.x = pointX;
		t.wpad.ir.y = pointY;
		t.wpad.ir.angle = 0.0f;
	}
}

int AutoInput_TakeExit(void)
{
	int ret = pendingExit;
	pendingExit = AUTOINPUT_EXIT_NONE;
	return ret;
}

int AutoInput_PromptAnswer(int buttons, const char *title, const char *msg)
{
	if (answersLeft <= 0 || buttons <= 0)
		return -1;
	answersLeft = answersLeft - 1;
	int button = answerButton > buttons ? buttons : answerButton;
	gprintf("autoinput answer: button %d of %d on \"%s\" (%s)\n", button, buttons, title ? title : "",
			msg ? msg : "");
	// WindowPrompt numbers its buttons 1, 2, ... and the last one 0
	return button != buttons ? button : 0;
}

const char *AutoInput_TakeAction(void)
{
	if (!actionPending)
		return NULL;
	snprintf(takenAction, sizeof(takenAction), "%s", pendingAction);
	actionPending = false;
	return takenAction;
}

#endif
