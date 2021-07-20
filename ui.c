/* Copyright (C) 2020  C. McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with OpenSSL (or a modified version of that library),
 * containing parts covered by the terms of the OpenSSL License and the
 * original SSLeay license, the licensors of this Program grant you
 * additional permission to convey the resulting work. Corresponding
 * Source for a non-source form of such a combination shall include the
 * source code for the parts of OpenSSL used as well as that of the
 * covered work.
 */

#define _XOPEN_SOURCE_EXTENDED

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <term.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#ifdef __FreeBSD__
#include <capsicum_helpers.h>
#endif

#include "chat.h"

// Annoying stuff from <term.h>:
#undef lines
#undef tab

enum {
	StatusLines = 1,
	MarkerLines = 1,
	SplitLines = 5,
	InputLines = 1,
	InputCols = 1024,
};

#define BOTTOM (LINES - 1)
#define RIGHT (COLS - 1)
#define MAIN_LINES (LINES - StatusLines - InputLines)

static WINDOW *status;
static WINDOW *main;
static WINDOW *input;

struct Window {
	uint id;
	int scroll;
	bool mark;
	bool mute;
	bool time;
	enum Heat thresh;
	enum Heat heat;
	uint unreadSoft;
	uint unreadHard;
	uint unreadWarm;
	struct Buffer *buffer;
};

static struct {
	struct Window *ptrs[IDCap];
	uint len;
	uint show;
	uint swap;
	uint user;
} windows;

static uint windowPush(struct Window *window) {
	assert(windows.len < IDCap);
	windows.ptrs[windows.len] = window;
	return windows.len++;
}

static uint windowInsert(uint num, struct Window *window) {
	assert(windows.len < IDCap);
	assert(num <= windows.len);
	memmove(
		&windows.ptrs[num + 1],
		&windows.ptrs[num],
		sizeof(*windows.ptrs) * (windows.len - num)
	);
	windows.ptrs[num] = window;
	windows.len++;
	return num;
}

static struct Window *windowRemove(uint num) {
	assert(num < windows.len);
	struct Window *window = windows.ptrs[num];
	windows.len--;
	memmove(
		&windows.ptrs[num],
		&windows.ptrs[num + 1],
		sizeof(*windows.ptrs) * (windows.len - num)
	);
	return window;
}

enum Heat uiThreshold = Cold;

static uint windowFor(uint id) {
	for (uint num = 0; num < windows.len; ++num) {
		if (windows.ptrs[num]->id == id) return num;
	}
	struct Window *window = calloc(1, sizeof(*window));
	if (!window) err(EX_OSERR, "malloc");
	window->id = id;
	window->mark = true;
	window->time = uiTime.enable;
	if (id == Network || id == Debug) {
		window->thresh = Cold;
	} else {
		window->thresh = uiThreshold;
	}
	window->buffer = bufferAlloc();
	completeAdd(None, idNames[id], idColors[id]);
	return windowPush(window);
}

static void windowFree(struct Window *window) {
	completeRemove(None, idNames[window->id]);
	bufferFree(window->buffer);
	free(window);
}

static short colorPairs;

static void colorInit(void) {
	start_color();
	use_default_colors();
	if (!COLORS) return;
	for (short pair = 0; pair < 16; ++pair) {
		init_pair(1 + pair, pair % COLORS, -1);
	}
	colorPairs = 17;
}

static attr_t colorAttr(short fg) {
	if (!COLORS) return (fg > 0 ? A_BOLD : A_NORMAL);
	if (fg != COLOR_BLACK && fg % COLORS == COLOR_BLACK) return A_BOLD;
	if (COLORS > 8) return A_NORMAL;
	return (fg / COLORS & 1 ? A_BOLD : A_NORMAL);
}

static short colorPair(short fg, short bg) {
	if (!COLORS) return 0;
	fg %= COLORS;
	bg %= COLORS;
	if (bg == -1 && fg < 16) return 1 + fg;
	for (short pair = 17; pair < colorPairs; ++pair) {
		short f, b;
		pair_content(pair, &f, &b);
		if (f == fg && b == bg) return pair;
	}
	init_pair(colorPairs, fg, bg);
	return colorPairs++;
}

#define ENUM_KEY \
	X(KeyCtrlLeft, "\33[1;5D", NULL) \
	X(KeyCtrlRight, "\33[1;5C", NULL) \
	X(KeyMeta0, "\0330", "\33)") \
	X(KeyMeta1, "\0331", "\33!") \
	X(KeyMeta2, "\0332", "\33@") \
	X(KeyMeta3, "\0333", "\33#") \
	X(KeyMeta4, "\0334", "\33$") \
	X(KeyMeta5, "\0335", "\33%") \
	X(KeyMeta6, "\0336", "\33^") \
	X(KeyMeta7, "\0337", "\33&") \
	X(KeyMeta8, "\0338", "\33*") \
	X(KeyMeta9, "\0339", "\33(") \
	X(KeyMetaA, "\33a", NULL) \
	X(KeyMetaB, "\33b", NULL) \
	X(KeyMetaD, "\33d", NULL) \
	X(KeyMetaF, "\33f", NULL) \
	X(KeyMetaL, "\33l", NULL) \
	X(KeyMetaM, "\33m", NULL) \
	X(KeyMetaN, "\33n", NULL) \
	X(KeyMetaP, "\33p", NULL) \
	X(KeyMetaQ, "\33q", NULL) \
	X(KeyMetaT, "\33t", NULL) \
	X(KeyMetaU, "\33u", NULL) \
	X(KeyMetaV, "\33v", NULL) \
	X(KeyMetaEnter, "\33\r", "\33\n") \
	X(KeyMetaGt, "\33>", "\33.") \
	X(KeyMetaLt, "\33<", "\33,") \
	X(KeyMetaEqual, "\33=", NULL) \
	X(KeyMetaMinus, "\33-", "\33_") \
	X(KeyMetaPlus, "\33+", NULL) \
	X(KeyMetaSlash, "\33/", "\33?") \
	X(KeyFocusIn, "\33[I", NULL) \
	X(KeyFocusOut, "\33[O", NULL) \
	X(KeyPasteOn, "\33[200~", NULL) \
	X(KeyPasteOff, "\33[201~", NULL) \
	X(KeyPasteManual, "\32p", "\32\20")

enum {
	KeyMax = KEY_MAX,
#define X(id, seq, alt) id,
	ENUM_KEY
#undef X
};

// XXX: Assuming terminals will be fine with these even if they're unsupported,
// since they're "private" modes.
static const char *FocusMode[2] = { "\33[?1004l", "\33[?1004h" };
static const char *PasteMode[2] = { "\33[?2004l", "\33[?2004h" };

struct Time uiTime = { .format = "%X" };

static void errExit(void) {
	putp(FocusMode[false]);
	putp(PasteMode[false]);
	reset_shell_mode();
}

void uiInitEarly(void) {
	initscr();
	cbreak();
	noecho();
	colorInit();
	atexit(errExit);

#ifndef A_ITALIC
#define A_ITALIC A_BLINK
	// Force ncurses to use individual enter_attr_mode strings:
	set_attributes = NULL;
	enter_blink_mode = enter_italics_mode;
#endif

	if (!to_status_line && !strncmp(termname(), "xterm", 5)) {
		to_status_line = "\33]2;";
		from_status_line = "\7";
	}

#define X(id, seq, alt) define_key(seq, id); if (alt) define_key(alt, id);
	ENUM_KEY
#undef X

	status = newwin(StatusLines, COLS, 0, 0);
	if (!status) err(EX_OSERR, "newwin");

	main = newwin(MAIN_LINES, COLS, StatusLines, 0);
	if (!main) err(EX_OSERR, "newwin");

	int y;
	char buf[TimeCap];
	struct tm *time = localtime(&(time_t) { -22100400 });
	size_t len = strftime(buf, sizeof(buf), uiTime.format, time);
	if (!len) errx(EX_CONFIG, "invalid timestamp format: %s", uiTime.format);
	waddstr(main, buf);
	waddch(main, ' ');
	getyx(main, y, uiTime.width);
	(void)y;

	input = newpad(InputLines, InputCols);
	if (!input) err(EX_OSERR, "newpad");
	keypad(input, true);
	nodelay(input, true);

	windowFor(Network);
	uiShow();
}

// Avoid disabling VINTR until main loop.
void uiInitLate(void) {
	struct termios term;
	int error = tcgetattr(STDOUT_FILENO, &term);
	if (error) err(EX_OSERR, "tcgetattr");

	// Gain use of C-q, C-s, C-c, C-z, C-y, C-v, C-o.
	term.c_iflag &= ~IXON;
	term.c_cc[VINTR] = _POSIX_VDISABLE;
	term.c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
	term.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
	term.c_cc[VLNEXT] = _POSIX_VDISABLE;
	term.c_cc[VDISCARD] = _POSIX_VDISABLE;

	error = tcsetattr(STDOUT_FILENO, TCSANOW, &term);
	if (error) err(EX_OSERR, "tcsetattr");

	def_prog_mode();
}

static bool hidden = true;
static bool waiting;

static char title[256];
static char prevTitle[sizeof(title)];

void uiDraw(void) {
	if (hidden) return;
	wnoutrefresh(status);
	wnoutrefresh(main);
	int y, x;
	getyx(input, y, x);
	pnoutrefresh(
		input,
		0, (x + 1 > RIGHT ? x + 1 - RIGHT : 0),
		LINES - InputLines, 0,
		BOTTOM, RIGHT
	);
	(void)y;
	doupdate();

	if (!to_status_line) return;
	if (!strcmp(title, prevTitle)) return;
	strcpy(prevTitle, title);
	putp(to_status_line);
	putp(title);
	putp(from_status_line);
	fflush(stdout);
}

static const short Colors[ColorCap] = {
	[Default]    = -1,
	[White]      = 8 + COLOR_WHITE,
	[Black]      = 0 + COLOR_BLACK,
	[Blue]       = 0 + COLOR_BLUE,
	[Green]      = 0 + COLOR_GREEN,
	[Red]        = 8 + COLOR_RED,
	[Brown]      = 0 + COLOR_RED,
	[Magenta]    = 0 + COLOR_MAGENTA,
	[Orange]     = 0 + COLOR_YELLOW,
	[Yellow]     = 8 + COLOR_YELLOW,
	[LightGreen] = 8 + COLOR_GREEN,
	[Cyan]       = 0 + COLOR_CYAN,
	[LightCyan]  = 8 + COLOR_CYAN,
	[LightBlue]  = 8 + COLOR_BLUE,
	[Pink]       = 8 + COLOR_MAGENTA,
	[Gray]       = 8 + COLOR_BLACK,
	[LightGray]  = 0 + COLOR_WHITE,
	52, 94, 100, 58, 22, 29, 23, 24, 17, 54, 53, 89,
	88, 130, 142, 64, 28, 35, 30, 25, 18, 91, 90, 125,
	124, 166, 184, 106, 34, 49, 37, 33, 19, 129, 127, 161,
	196, 208, 226, 154, 46, 86, 51, 75, 21, 171, 201, 198,
	203, 215, 227, 191, 83, 122, 87, 111, 63, 177, 207, 205,
	217, 223, 229, 193, 157, 158, 159, 153, 147, 183, 219, 212,
	16, 233, 235, 237, 239, 241, 244, 247, 250, 254, 231,
};

static attr_t styleAttr(struct Style style) {
	attr_t attr = A_NORMAL;
	if (style.attr & Bold) attr |= A_BOLD;
	if (style.attr & Reverse) attr |= A_REVERSE;
	if (style.attr & Italic) attr |= A_ITALIC;
	if (style.attr & Underline) attr |= A_UNDERLINE;
	return attr | colorAttr(Colors[style.fg]);
}

static short stylePair(struct Style style) {
	return colorPair(Colors[style.fg], Colors[style.bg]);
}

static int styleAdd(WINDOW *win, const char *str) {
	struct Style style = StyleDefault;
	while (*str) {
		size_t len = styleParse(&style, &str);
		wattr_set(win, styleAttr(style), stylePair(style), NULL);
		if (waddnstr(win, str, len) == ERR)
			return -1;
		str += len;
	}
	return 0;
}

static void statusUpdate(void) {
	struct {
		uint unread;
		enum Heat heat;
	} others = { 0, Cold };

	wmove(status, 0, 0);
	for (uint num = 0; num < windows.len; ++num) {
		const struct Window *window = windows.ptrs[num];
		if (num != windows.show && !window->scroll) {
			if (window->heat < Warm) continue;
			if (window->mute && window->heat < Hot) continue;
		}
		if (num != windows.show) {
			others.unread += window->unreadWarm;
			if (window->heat > others.heat) others.heat = window->heat;
		}
		char buf[256], *end = &buf[sizeof(buf)];
		char *ptr = seprintf(
			buf, end, "\3%d%s %u%s%s %s ",
			idColors[window->id], (num == windows.show ? "\26" : ""),
			num, window->thresh[(const char *[]) { "-", "", "+", "++" }],
			&"="[!window->mute], idNames[window->id]
		);
		if (window->mark && window->unreadWarm) {
			ptr = seprintf(
				ptr, end, "\3%d+%d\3%d%s",
				(window->heat > Warm ? White : idColors[window->id]),
				window->unreadWarm, idColors[window->id],
				(window->scroll ? "" : " ")
			);
		}
		if (window->scroll) {
			ptr = seprintf(ptr, end, "~%d ", window->scroll);
		}
		if (styleAdd(status, buf) < 0) break;
	}
	wclrtoeol(status);

	const struct Window *window = windows.ptrs[windows.show];
	char *end = &title[sizeof(title)];
	char *ptr = seprintf(
		title, end, "%s %s", network.name, idNames[window->id]
	);
	if (window->mark && window->unreadWarm) {
		ptr = seprintf(
			ptr, end, " +%d%s", window->unreadWarm, &"!"[window->heat < Hot]
		);
	}
	if (others.unread) {
		ptr = seprintf(
			ptr, end, " (+%d%s)", others.unread, &"!"[others.heat < Hot]
		);
	}
}

static void mark(struct Window *window) {
	if (window->scroll) return;
	window->mark = true;
	window->unreadSoft = 0;
	window->unreadWarm = 0;
}

static void unmark(struct Window *window) {
	if (!window->scroll) {
		window->mark = false;
		window->heat = Cold;
	}
	statusUpdate();
}

void uiShow(void) {
	if (!hidden) return;
	prevTitle[0] = '\0';
	putp(FocusMode[true]);
	putp(PasteMode[true]);
	fflush(stdout);
	hidden = false;
	unmark(windows.ptrs[windows.show]);
}

void uiHide(void) {
	if (hidden) return;
	mark(windows.ptrs[windows.show]);
	hidden = true;
	putp(FocusMode[false]);
	putp(PasteMode[false]);
	endwin();
}

static size_t windowTop(const struct Window *window) {
	size_t top = BufferCap - MAIN_LINES - window->scroll;
	if (window->scroll) top += MarkerLines;
	return top;
}

static size_t windowBottom(const struct Window *window) {
	size_t bottom = BufferCap - (window->scroll ?: 1);
	if (window->scroll) bottom -= SplitLines + MarkerLines;
	return bottom;
}

static int windowCols(const struct Window *window) {
	return COLS - (window->time ? uiTime.width : 0);
}

static void mainAdd(int y, bool time, const struct Line *line) {
	int ny, nx;
	wmove(main, y, 0);
	if (!line || !line->str[0]) {
		wclrtoeol(main);
		return;
	}
	if (time && line->time) {
		char buf[TimeCap];
		strftime(buf, sizeof(buf), uiTime.format, localtime(&line->time));
		wattr_set(
			main,
			colorAttr(Colors[Gray]), colorPair(Colors[Gray], -1),
			NULL
		);
		waddstr(main, buf);
		waddch(main, ' ');
	} else if (time) {
		whline(main, ' ', uiTime.width);
		wmove(main, y, uiTime.width);
	}
	styleAdd(main, line->str);
	getyx(main, ny, nx);
	if (ny != y) return;
	wclrtoeol(main);
	(void)nx;
}

static void mainUpdate(void) {
	struct Window *window = windows.ptrs[windows.show];

	int y = 0;
	int marker = MAIN_LINES - SplitLines - MarkerLines;
	for (size_t i = windowTop(window); i < BufferCap; ++i) {
		mainAdd(y++, window->time, bufferHard(window->buffer, i));
		if (window->scroll && y == marker) break;
	}
	if (!window->scroll) return;

	y = MAIN_LINES - SplitLines;
	for (size_t i = BufferCap - SplitLines; i < BufferCap; ++i) {
		mainAdd(y++, window->time, bufferHard(window->buffer, i));
	}
	wattr_set(main, A_NORMAL, 0, NULL);
	mvwhline(main, marker, 0, ACS_BULLET, COLS);
}

static void windowScroll(struct Window *window, int n) {
	mark(window);
	window->scroll += n;
	if (window->scroll > BufferCap - MAIN_LINES) {
		window->scroll = BufferCap - MAIN_LINES;
	}
	if (window->scroll < 0) window->scroll = 0;
	unmark(window);
	if (window == windows.ptrs[windows.show]) mainUpdate();
}

struct Util uiNotifyUtil;
static void notify(uint id, const char *str) {
	if (self.restricted) return;
	if (!uiNotifyUtil.argc) return;

	char buf[1024];
	styleStrip(buf, sizeof(buf), str);

	struct Util util = uiNotifyUtil;
	utilPush(&util, idNames[id]);
	utilPush(&util, buf);

	pid_t pid = fork();
	if (pid < 0) err(EX_OSERR, "fork");
	if (pid) return;

	setsid();
	close(STDIN_FILENO);
	dup2(utilPipe[1], STDOUT_FILENO);
	dup2(utilPipe[1], STDERR_FILENO);
	execvp(util.argv[0], (char *const *)util.argv);
	warn("%s", util.argv[0]);
	_exit(EX_CONFIG);
}

void uiWrite(uint id, enum Heat heat, const time_t *src, const char *str) {
	struct Window *window = windows.ptrs[windowFor(id)];
	time_t ts = (src ? *src : time(NULL));

	if (heat >= window->thresh) {
		if (!window->unreadSoft++) window->unreadHard = 0;
	}
	if (window->mark && heat > Cold) {
		if (!window->unreadWarm++) {
			int lines = bufferPush(
				window->buffer, windowCols(window),
				window->thresh, Warm, ts, ""
			);
			if (window->scroll) windowScroll(window, lines);
			if (window->unreadSoft > 1) {
				window->unreadSoft++;
				window->unreadHard += lines;
			}
		}
		if (heat > window->heat) window->heat = heat;
		statusUpdate();
	}
	int lines = bufferPush(
		window->buffer, windowCols(window),
		window->thresh, heat, ts, str
	);
	window->unreadHard += lines;
	if (window->scroll) windowScroll(window, lines);
	if (window == windows.ptrs[windows.show]) mainUpdate();

	if (window->mark && heat > Warm) {
		beep();
		notify(id, str);
	}
}

void uiFormat(
	uint id, enum Heat heat, const time_t *time, const char *format, ...
) {
	char buf[1024];
	va_list ap;
	va_start(ap, format);
	int len = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	assert((size_t)len < sizeof(buf));
	uiWrite(id, heat, time, buf);
}

static void scrollTo(struct Window *window, int top) {
	window->scroll = 0;
	windowScroll(window, top - MAIN_LINES + MarkerLines);
}

static void windowReflow(struct Window *window) {
	uint num = 0;
	const struct Line *line = bufferHard(window->buffer, windowTop(window));
	if (line) num = line->num;
	window->unreadHard = bufferReflow(
		window->buffer, windowCols(window),
		window->thresh, window->unreadSoft
	);
	if (!window->scroll || !num) return;
	for (size_t i = 0; i < BufferCap; ++i) {
		line = bufferHard(window->buffer, i);
		if (!line || line->num != num) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

static void resize(void) {
	wclear(main);
	wresize(main, MAIN_LINES, COLS);
	for (uint num = 0; num < windows.len; ++num) {
		windowReflow(windows.ptrs[num]);
	}
	statusUpdate();
	mainUpdate();
}

static void windowList(const struct Window *window) {
	uiHide();
	waiting = true;

	uint num = 0;
	const struct Line *line = bufferHard(window->buffer, windowBottom(window));
	if (line) num = line->num;
	for (size_t i = 0; i < BufferCap; ++i) {
		line = bufferSoft(window->buffer, i);
		if (!line) continue;
		if (line->num > num) break;
		if (!line->str[0]) {
			printf("\n");
			continue;
		}

		char buf[TimeCap];
		strftime(buf, sizeof(buf), uiTime.format, localtime(&line->time));
		vid_attr(colorAttr(Colors[Gray]), colorPair(Colors[Gray], -1), NULL);
		printf("%s ", buf);

		bool align = false;
		struct Style style = StyleDefault;
		for (const char *str = line->str; *str;) {
			if (*str == '\t') {
				printf("%c", (align ? '\t' : ' '));
				align = true;
				str++;
			}

			size_t len = styleParse(&style, &str);
			size_t tab = strcspn(str, "\t");
			if (tab < len) len = tab;

			vid_attr(styleAttr(style), stylePair(style), NULL);
			printf("%.*s", (int)len, str);
			str += len;
		}
		printf("\n");
	}
}

static void inputAdd(struct Style reset, struct Style *style, const char *str) {
	while (*str) {
		const char *code = str;
		size_t len = styleParse(style, &str);
		wattr_set(input, A_BOLD | A_REVERSE, 0, NULL);
		switch (*code) {
			break; case B: waddch(input, 'B');
			break; case C: waddch(input, 'C');
			break; case O: waddch(input, 'O');
			break; case R: waddch(input, 'R');
			break; case I: waddch(input, 'I');
			break; case U: waddch(input, 'U');
			break; case '\n': waddch(input, 'N');
		}
		if (str - code > 1) waddnstr(input, &code[1], str - &code[1]);
		if (str[0] == '\n') {
			*style = reset;
			str++;
			len--;
		}
		size_t nl = strcspn(str, "\n");
		if (nl < len) len = nl;
		wattr_set(input, styleAttr(*style), stylePair(*style), NULL);
		waddnstr(input, str, len);
		str += len;
	}
}

static char *inputStop(
	struct Style reset, struct Style *style,
	const char *str, char *stop
) {
	char ch = *stop;
	*stop = '\0';
	inputAdd(reset, style, str);
	*stop = ch;
	return stop;
}

static void inputUpdate(void) {
	size_t pos;
	char *buf = editBuffer(&pos);
	struct Window *window = windows.ptrs[windows.show];

	const char *prefix = "";
	const char *prompt = self.nick;
	const char *suffix = "";
	const char *skip = buf;
	struct Style stylePrompt = { .fg = self.color, .bg = Default };
	struct Style styleInput = StyleDefault;

	size_t split = commandWillSplit(window->id, buf);
	const char *privmsg = commandIsPrivmsg(window->id, buf);
	const char *notice = commandIsNotice(window->id, buf);
	const char *action = commandIsAction(window->id, buf);
	if (privmsg) {
		prefix = "<"; suffix = "> ";
		skip = privmsg;
	} else if (notice) {
		prefix = "-"; suffix = "- ";
		styleInput.fg = LightGray;
		skip = notice;
	} else if (action) {
		prefix = "* "; suffix = " ";
		stylePrompt.attr |= Italic;
		styleInput.attr |= Italic;
		skip = action;
	} else if (window->id == Debug && buf[0] != '/') {
		prompt = "<< ";
		stylePrompt.fg = Gray;
	} else {
		prompt = "";
	}
	if (skip > &buf[pos]) {
		prefix = prompt = suffix = "";
		skip = buf;
	}

	wmove(input, 0, 0);
	if (window->time && window->id != Network) {
		whline(input, ' ', uiTime.width);
		wmove(input, 0, uiTime.width);
	}
	wattr_set(input, styleAttr(stylePrompt), stylePair(stylePrompt), NULL);
	waddstr(input, prefix);
	waddstr(input, prompt);
	waddstr(input, suffix);

	int y, x;
	const char *ptr = skip;
	struct Style style = styleInput;
	if (split && split < pos) {
		ptr = inputStop(styleInput, &style, ptr, &buf[split]);
		style = styleInput;
		style.bg = Red;
	}
	ptr = inputStop(styleInput, &style, ptr, &buf[pos]);
	getyx(input, y, x);
	if (split && split >= pos) {
		ptr = inputStop(styleInput, &style, ptr, &buf[split]);
		style = styleInput;
		style.bg = Red;
	}
	inputAdd(styleInput, &style, ptr);
	wclrtoeol(input);
	wmove(input, y, x);
}

void uiWindows(void) {
	for (uint num = 0; num < windows.len; ++num) {
		const struct Window *window = windows.ptrs[num];
		uiFormat(
			Network, Warm, NULL, "\3%02d%u %s",
			idColors[window->id], num, idNames[window->id]
		);
	}
}

static void windowShow(uint num) {
	if (num != windows.show) {
		windows.swap = windows.show;
		mark(windows.ptrs[windows.swap]);
	}
	windows.show = num;
	windows.user = num;
	unmark(windows.ptrs[windows.show]);
	mainUpdate();
	inputUpdate();
}

void uiShowID(uint id) {
	windowShow(windowFor(id));
}

void uiShowNum(uint num) {
	if (num < windows.len) windowShow(num);
}

void uiMoveID(uint id, uint num) {
	struct Window *window = windowRemove(windowFor(id));
	if (num < windows.len) {
		windowShow(windowInsert(num, window));
	} else {
		windowShow(windowPush(window));
	}
}

static void windowClose(uint num) {
	if (windows.ptrs[num]->id == Network) return;
	struct Window *window = windowRemove(num);
	completeClear(window->id);
	windowFree(window);
	if (windows.swap >= num) windows.swap--;
	if (windows.show == num) {
		windowShow(windows.swap);
		windows.swap = windows.show;
	} else if (windows.show > num) {
		windows.show--;
		mainUpdate();
	}
	statusUpdate();
}

void uiCloseID(uint id) {
	windowClose(windowFor(id));
}

void uiCloseNum(uint num) {
	if (num < windows.len) windowClose(num);
}

static void scrollPage(struct Window *window, int n) {
	windowScroll(window, n * (MAIN_LINES - SplitLines - MarkerLines - 1));
}

static void scrollTop(struct Window *window) {
	for (size_t i = 0; i < BufferCap; ++i) {
		if (!bufferHard(window->buffer, i)) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

static void scrollHot(struct Window *window, int dir) {
	for (size_t i = windowTop(window) + dir; i < BufferCap; i += dir) {
		const struct Line *line = bufferHard(window->buffer, i);
		const struct Line *prev = bufferHard(window->buffer, i - 1);
		if (!line || line->heat < Hot) continue;
		if (prev && prev->heat > Warm) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

static void scrollSearch(struct Window *window, const char *str, int dir) {
	for (size_t i = windowTop(window) + dir; i < BufferCap; i += dir) {
		const struct Line *line = bufferHard(window->buffer, i);
		if (!line || !strcasestr(line->str, str)) continue;
		scrollTo(window, BufferCap - i);
		break;
	}
}

static void toggleTime(struct Window *window) {
	window->time ^= true;
	windowReflow(window);
	statusUpdate();
	mainUpdate();
	inputUpdate();
}

static void incThresh(struct Window *window, int n) {
	if (n > 0 && window->thresh == Hot) return;
	if (n < 0 && window->thresh == Ice) {
		window->thresh = Cold;
	} else {
		window->thresh += n;
	}
	windowReflow(window);
	statusUpdate();
	mainUpdate();
	statusUpdate();
}

static void showAuto(void) {
	uint minHot = UINT_MAX, numHot = 0;
	uint minWarm = UINT_MAX, numWarm = 0;
	for (uint num = 0; num < windows.len; ++num) {
		struct Window *window = windows.ptrs[num];
		if (window->heat >= Hot) {
			if (window->unreadWarm >= minHot) continue;
			minHot = window->unreadWarm;
			numHot = num;
		}
		if (window->heat >= Warm && !window->mute) {
			if (window->unreadWarm >= minWarm) continue;
			minWarm = window->unreadWarm;
			numWarm = num;
		}
	}
	uint user = windows.user;
	if (minHot < UINT_MAX) {
		windowShow(numHot);
		windows.user = user;
	} else if (minWarm < UINT_MAX) {
		windowShow(numWarm);
		windows.user = user;
	} else if (user != windows.show) {
		windowShow(user);
	}
}

static void keyCode(int code) {
	struct Window *window = windows.ptrs[windows.show];
	uint id = window->id;
	switch (code) {
		break; case KEY_RESIZE:  resize();
		break; case KeyFocusIn:  unmark(window);
		break; case KeyFocusOut: mark(window);

		break; case KeyMetaEnter: edit(id, EditInsert, L'\n');
		break; case KeyMetaEqual: window->mute ^= true; statusUpdate();
		break; case KeyMetaMinus: incThresh(window, -1);
		break; case KeyMetaPlus:  incThresh(window, +1);
		break; case KeyMetaSlash: windowShow(windows.swap);

		break; case KeyMetaGt: scrollTo(window, 0);
		break; case KeyMetaLt: scrollTop(window);

		break; case KeyMeta0 ... KeyMeta9: uiShowNum(code - KeyMeta0);
		break; case KeyMetaA: showAuto();
		break; case KeyMetaB: edit(id, EditPrevWord, 0);
		break; case KeyMetaD: edit(id, EditDeleteNextWord, 0);
		break; case KeyMetaF: edit(id, EditNextWord, 0);
		break; case KeyMetaL: windowList(window);
		break; case KeyMetaM: uiWrite(id, Warm, NULL, "");
		break; case KeyMetaN: scrollHot(window, +1);
		break; case KeyMetaP: scrollHot(window, -1);
		break; case KeyMetaQ: edit(id, EditCollapse, 0);
		break; case KeyMetaT: toggleTime(window);
		break; case KeyMetaU: scrollTo(window, window->unreadHard);
		break; case KeyMetaV: scrollPage(window, +1);

		break; case KeyCtrlLeft: edit(id, EditPrevWord, 0);
		break; case KeyCtrlRight: edit(id, EditNextWord, 0);

		break; case KEY_BACKSPACE: edit(id, EditDeletePrev, 0);
		break; case KEY_DC: edit(id, EditDeleteNext, 0);
		break; case KEY_DOWN: windowScroll(window, -1);
		break; case KEY_END: edit(id, EditTail, 0);
		break; case KEY_ENTER: edit(id, EditEnter, 0);
		break; case KEY_HOME: edit(id, EditHead, 0);
		break; case KEY_LEFT: edit(id, EditPrev, 0);
		break; case KEY_NPAGE: scrollPage(window, -1);
		break; case KEY_PPAGE: scrollPage(window, +1);
		break; case KEY_RIGHT: edit(id, EditNext, 0);
		break; case KEY_SEND: scrollTo(window, 0);
		break; case KEY_SHOME: scrollTo(window, BufferCap);
		break; case KEY_UP: windowScroll(window, +1);
	}
}

static void keyCtrl(wchar_t ch) {
	struct Window *window = windows.ptrs[windows.show];
	uint id = window->id;
	switch (ch ^ L'@') {
		break; case L'?': edit(id, EditDeletePrev, 0);
		break; case L'A': edit(id, EditHead, 0);
		break; case L'B': edit(id, EditPrev, 0);
		break; case L'C': raise(SIGINT);
		break; case L'D': edit(id, EditDeleteNext, 0);
		break; case L'E': edit(id, EditTail, 0);
		break; case L'F': edit(id, EditNext, 0);
		break; case L'H': edit(id, EditDeletePrev, 0);
		break; case L'I': edit(id, EditComplete, 0);
		break; case L'J': edit(id, EditEnter, 0);
		break; case L'K': edit(id, EditDeleteTail, 0);
		break; case L'L': clearok(curscr, true);
		break; case L'N': uiShowNum(windows.show + 1);
		break; case L'P': uiShowNum(windows.show - 1);
		break; case L'R': scrollSearch(window, editBuffer(NULL), -1);
		break; case L'S': scrollSearch(window, editBuffer(NULL), +1);
		break; case L'T': edit(id, EditTranspose, 0);
		break; case L'U': edit(id, EditDeleteHead, 0);
		break; case L'V': scrollPage(window, -1);
		break; case L'W': edit(id, EditDeletePrevWord, 0);
		break; case L'X': edit(id, EditExpand, 0);
		break; case L'Y': edit(id, EditPaste, 0);
	}
}

static void keyStyle(wchar_t ch) {
	uint id = windows.ptrs[windows.show]->id;
	if (iswcntrl(ch)) ch = towlower(ch ^ L'@');
	enum Color color = Default;
	switch (ch) {
		break; case L'A': color = Gray;
		break; case L'B': color = Blue;
		break; case L'C': color = Cyan;
		break; case L'G': color = Green;
		break; case L'K': color = Black;
		break; case L'M': color = Magenta;
		break; case L'N': color = Brown;
		break; case L'O': color = Orange;
		break; case L'P': color = Pink;
		break; case L'R': color = Red;
		break; case L'W': color = White;
		break; case L'Y': color = Yellow;
		break; case L'b': edit(id, EditInsert, B);
		break; case L'c': edit(id, EditInsert, C);
		break; case L'i': edit(id, EditInsert, I);
		break; case L'o': edit(id, EditInsert, O);
		break; case L'r': edit(id, EditInsert, R);
		break; case L'u': edit(id, EditInsert, U);
	}
	if (color != Default) {
		char buf[4];
		snprintf(buf, sizeof(buf), "%c%02d", C, color);
		for (char *ch = buf; *ch; ++ch) {
			edit(id, EditInsert, *ch);
		}
	}
}

void uiRead(void) {
	if (hidden) {
		if (waiting) {
			uiShow();
			flushinp();
			waiting = false;
		} else {
			return;
		}
	}

	wint_t ch;
	static bool paste, style, literal;
	for (int ret; ERR != (ret = wget_wch(input, &ch));) {
		if (ret == KEY_CODE_YES && ch == KeyPasteOn) {
			paste = true;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteOff) {
			paste = false;
		} else if (ret == KEY_CODE_YES && ch == KeyPasteManual) {
			paste ^= true;
		} else if (paste || literal) {
			edit(windows.ptrs[windows.show]->id, EditInsert, ch);
		} else if (ret == KEY_CODE_YES) {
			keyCode(ch);
		} else if (ch == (L'Z' ^ L'@')) {
			style = true;
			continue;
		} else if (style && ch == (L'V' ^ L'@')) {
			literal = true;
			continue;
		} else if (style) {
			keyStyle(ch);
		} else if (iswcntrl(ch)) {
			keyCtrl(ch);
		} else {
			edit(windows.ptrs[windows.show]->id, EditInsert, ch);
		}
		style = false;
		literal = false;
	}
	inputUpdate();
}

static const time_t Signatures[] = {
	0x6C72696774616301, // no heat, unread, unreadWarm
	0x6C72696774616302, // no self.pos
	0x6C72696774616303, // no buffer line heat
	0x6C72696774616304, // no mute
	0x6C72696774616305, // no URLs
	0x6C72696774616306, // no thresh
	0x6C72696774616307, // no window time
	0x6C72696774616308,
};

static size_t signatureVersion(time_t signature) {
	for (size_t i = 0; i < ARRAY_LEN(Signatures); ++i) {
		if (signature == Signatures[i]) return i;
	}
	errx(EX_DATAERR, "unknown file signature %jX", (uintmax_t)signature);
}

static int writeTime(FILE *file, time_t time) {
	return (fwrite(&time, sizeof(time), 1, file) ? 0 : -1);
}
static int writeString(FILE *file, const char *str) {
	return (fwrite(str, strlen(str) + 1, 1, file) ? 0 : -1);
}

static FILE *saveFile;

int uiSave(void) {
	int error = 0
		|| ftruncate(fileno(saveFile), 0)
		|| writeTime(saveFile, Signatures[7])
		|| writeTime(saveFile, self.pos);
	if (error) return error;
	for (uint num = 0; num < windows.len; ++num) {
		const struct Window *window = windows.ptrs[num];
		error = 0
			|| writeString(saveFile, idNames[window->id])
			|| writeTime(saveFile, window->mute)
			|| writeTime(saveFile, window->time)
			|| writeTime(saveFile, window->thresh)
			|| writeTime(saveFile, window->heat)
			|| writeTime(saveFile, window->unreadSoft)
			|| writeTime(saveFile, window->unreadWarm);
		if (error) return error;
		for (size_t i = 0; i < BufferCap; ++i) {
			const struct Line *line = bufferSoft(window->buffer, i);
			if (!line) continue;
			error = 0
				|| writeTime(saveFile, line->time)
				|| writeTime(saveFile, line->heat)
				|| writeString(saveFile, line->str);
			if (error) return error;
		}
		error = writeTime(saveFile, 0);
		if (error) return error;
	}
	return 0
		|| writeString(saveFile, "")
		|| urlSave(saveFile)
		|| fclose(saveFile);
}

static time_t readTime(FILE *file) {
	time_t time;
	fread(&time, sizeof(time), 1, file);
	if (ferror(file)) err(EX_IOERR, "fread");
	if (feof(file)) errx(EX_DATAERR, "unexpected eof");
	return time;
}
static ssize_t readString(FILE *file, char **buf, size_t *cap) {
	ssize_t len = getdelim(buf, cap, '\0', file);
	if (len < 0 && !feof(file)) err(EX_IOERR, "getdelim");
	return len;
}

void uiLoad(const char *name) {
	int error;
	saveFile = dataOpen(name, "a+e");
	if (!saveFile) exit(EX_CANTCREAT);
	rewind(saveFile);

#ifdef __FreeBSD__
	cap_rights_t rights;
	cap_rights_init(&rights, CAP_READ, CAP_WRITE, CAP_FLOCK, CAP_FTRUNCATE);
	error = caph_rights_limit(fileno(saveFile), &rights);
	if (error) err(EX_OSERR, "cap_rights_limit");
#endif

	error = flock(fileno(saveFile), LOCK_EX | LOCK_NB);
	if (error && errno == EWOULDBLOCK) {
		errx(EX_CANTCREAT, "%s: save file in use", name);
	}

	time_t signature;
	fread(&signature, sizeof(signature), 1, saveFile);
	if (ferror(saveFile)) err(EX_IOERR, "fread");
	if (feof(saveFile)) {
		return;
	}
	size_t version = signatureVersion(signature);

	if (version > 1) {
		self.pos = readTime(saveFile);
	}

	char *buf = NULL;
	size_t cap = 0;
	while (0 < readString(saveFile, &buf, &cap) && buf[0]) {
		struct Window *window = windows.ptrs[windowFor(idFor(buf))];
		if (version > 3) window->mute = readTime(saveFile);
		if (version > 6) window->time = readTime(saveFile);
		if (version > 5) window->thresh = readTime(saveFile);
		if (version > 0) {
			window->heat = readTime(saveFile);
			window->unreadSoft = readTime(saveFile);
			window->unreadWarm = readTime(saveFile);
		}
		for (;;) {
			time_t time = readTime(saveFile);
			if (!time) break;
			enum Heat heat = (version > 2 ? readTime(saveFile) : Cold);
			readString(saveFile, &buf, &cap);
			bufferPush(window->buffer, COLS, window->thresh, heat, time, buf);
		}
		windowReflow(window);
	}
	urlLoad(saveFile, version);

	free(buf);
}
