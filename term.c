/* Copyright (C) 2018  Curtis McEnroe <june@causal.agency>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chat.h"


static bool xterm;

void termInit(void) {
	char *term = getenv("TERM");
	xterm = term && !strncmp(term, "xterm", 5);
}

void termTitle(const char *title) {
	if (!xterm) return;
	printf("\33]0;%s\33\\", title);
	fflush(stdout);
}

static void privateMode(const char *mode, bool set) {
	printf("\33[?%s%c", mode, (set ? 'h' : 'l'));
	fflush(stdout);
}

void termMode(enum TermMode mode, bool set) {
	switch (mode) {
		break; case TERM_FOCUS: privateMode("1004", set);
		break; case TERM_PASTE: privateMode("2004", set);
	}
}

#define ESC '\33'
#define T(s, i) ((s) << 8 | (i))

enum TermEvent termEvent(char ch) {
	static int state = 0;
	switch (T(state, ch)) {
		case T(0, ESC): state = 1; return 0;
		case T(1, '['): state = 2; return 0;
		case T(2, 'I'): state = 0; return TERM_FOCUS_IN;
		case T(2, 'O'): state = 0; return TERM_FOCUS_OUT;
		case T(2, '2'): state = 3; return 0;
		case T(3, '0'): state = 4; return 0;
		case T(4, '0'): state = 5; return 0;
		case T(5, '~'): state = 0; return TERM_PASTE_START;
		case T(4, '1'): state = 6; return 0;
		case T(6, '~'): state = 0; return TERM_PASTE_END;
		default:        state = 0; return 0;
	}
}
