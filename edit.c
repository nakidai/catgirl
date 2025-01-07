/* Copyright (C) 2020, 2022  June McEnroe <june@causal.agency>
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

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "edit.h"

static bool isword(wchar_t ch) {
	return !iswspace(ch) && !iswpunct(ch);
}

char *editString(const struct Edit *e, char **buf, size_t *cap, size_t *pos) {
	size_t req = e->len * MB_CUR_MAX + 1;
	if (req > *cap) {
		char *new = realloc(*buf, req);
		if (!new) return NULL;
		*buf = new;
		*cap = req;
	}

	const wchar_t *ptr = e->buf;
	size_t len = wcsnrtombs(*buf, &ptr, e->pos, *cap-1, NULL);
	if (len == (size_t)-1) return NULL;
	if (pos) *pos = len;

	ptr = &e->buf[e->pos];
	size_t n = wcsnrtombs(
		*buf + len, &ptr, e->len - e->pos, *cap-1 - len, NULL
	);
	if (n == (size_t)-1) return NULL;
	len += n;

	(*buf)[len] = '\0';
	return *buf;
}

int editReserve(struct Edit *e, size_t index, size_t count) {
	if (index > e->len) {
		errno = EINVAL;
		return -1;
	}
	if (e->len + count > e->cap) {
		size_t cap = (e->cap ?: 256);
		while (cap < e->len + count) cap *= 2;
		wchar_t *buf = realloc(e->buf, sizeof(*buf) * cap);
		if (!buf) return -1;
		e->buf = buf;
		e->cap = cap;
	}
	wmemmove(&e->buf[index + count], &e->buf[index], e->len - index);
	e->len += count;
	return 0;
}

int editCopy(struct Edit *e, size_t index, size_t count) {
	if (index + count > e->len) {
		errno = EINVAL;
		return -1;
	}
	if (!e->cut) return 0;
	e->cut->len = 0;
	if (editReserve(e->cut, 0, count) < 0) return -1;
	wmemcpy(e->cut->buf, &e->buf[index], count);
	return 0;
}

int editDelete(struct Edit *e, bool cut, size_t index, size_t count) {
	if (index + count > e->len) {
		errno = EINVAL;
		return -1;
	}
	if (cut && editCopy(e, index, count) < 0) return -1;
	wmemmove(&e->buf[index], &e->buf[index + count], e->len - index - count);
	e->len -= count;
	if (e->pos > e->len) e->pos = e->len;
	return 0;
}

static size_t prevSpacing(const struct Edit *e, size_t pos) {
	if (!pos) return 0;
	do {
		pos--;
	} while (pos && !wcwidth(e->buf[pos]));
	return pos;
}

static size_t nextSpacing(const struct Edit *e, size_t pos) {
	if (pos == e->len) return e->len;
	do {
		pos++;
	} while (pos < e->len && !wcwidth(e->buf[pos]));
	return pos;
}

int editFn(struct Edit *e, enum EditFn fn) {
	int ret = 0;
	switch (fn) {
		break; case EditHead: e->pos = 0;
		break; case EditTail: e->pos = e->len;
		break; case EditPrev: e->pos = prevSpacing(e, e->pos);
		break; case EditNext: e->pos = nextSpacing(e, e->pos);
		break; case EditPrevWord: {
			while (e->pos && !isword(e->buf[e->pos-1])) e->pos--;
			while (e->pos && isword(e->buf[e->pos-1])) e->pos--;
		}
		break; case EditNextWord: {
			while (e->pos < e->len && isword(e->buf[e->pos])) e->pos++;
			while (e->pos < e->len && !isword(e->buf[e->pos])) e->pos++;
		}

		break; case EditDeleteHead: {
			ret = editDelete(e, true, 0, e->pos);
			e->pos = 0;
		}
		break; case EditDeleteTail: {
			ret = editDelete(e, true, e->pos, e->len - e->pos);
		}
		break; case EditDeletePrev: {
			size_t prev = prevSpacing(e, e->pos);
			editDelete(e, false, prev, e->pos - prev);
			e->pos = prev;
		}
		break; case EditDeleteNext: {
			editDelete(e, false, e->pos, nextSpacing(e, e->pos) - e->pos);
		}
		break; case EditDeletePrevWord: {
			if (!e->pos) break;
			size_t word = e->pos;
			while (word && !isword(e->buf[word-1])) word--;
			while (word && isword(e->buf[word-1])) word--;
			ret = editDelete(e, true, word, e->pos - word);
			e->pos = word;
		}
		break; case EditDeleteNextWord: {
			if (e->pos == e->len) break;
			size_t word = e->pos;
			while (word < e->len && !isword(e->buf[word])) word++;
			while (word < e->len && isword(e->buf[word])) word++;
			ret = editDelete(e, true, e->pos, word - e->pos);
		}

		break; case EditPaste: {
			if (!e->cut) break;
			ret = editReserve(e, e->pos, e->cut->len);
			if (ret == 0) {
				wmemcpy(&e->buf[e->pos], e->cut->buf, e->cut->len);
				e->pos += e->cut->len;
			}
		}
		break; case EditTranspose: {
			if (e->len < 2) break;
			if (!e->pos) e->pos++;
			if (e->pos == e->len) e->pos--;
			wchar_t x = e->buf[e->pos-1];
			e->buf[e->pos-1] = e->buf[e->pos];
			e->buf[e->pos++] = x;
		}
		break; case EditCollapse: {
			size_t ws;
			for (e->pos = 0; e->pos < e->len;) {
				for (; e->pos < e->len && !iswspace(e->buf[e->pos]); ++e->pos);
				for (ws = e->pos; ws < e->len && iswspace(e->buf[ws]); ++ws);
				if (e->pos && ws < e->len) {
					editDelete(e, false, e->pos, ws - e->pos - 1);
					e->buf[e->pos++] = L' ';
				} else {
					editDelete(e, false, e->pos, ws - e->pos);
				}
			}
		}

		break; case EditClear: e->len = e->pos = 0;
	}
	return ret;
}

static bool isvalid(wchar_t ch) {
	char mb[MB_LEN_MAX];
	return wctomb(mb, ch) > 0;
}

int editInsert(struct Edit *e, wchar_t ch) {
	if (!isvalid(ch)) return -1;
	if (editReserve(e, e->pos, 1) < 0) return -1;
	e->buf[e->pos++] = ch;
	return 0;
}

enum {
	Esc = L'\33',
	Erase = L'\177',
	Kill = L'@' ^ L'U',
	LNext = L'@' ^ 'V',
	WErase = L'@' ^ L'W',
};

static void viEscape(struct Edit *e) {
	e->vi.mode = EditViCommand;
	e->vi.count = 0;
	e->vi.verb = '\0';
}

static int viInsert(struct Edit *e, wchar_t ch) {
	if (!e->vi.lnext) {
		switch (ch) {
			break; case Erase:  return editFn(e, EditDeletePrev);
			break; case Kill:   return editFn(e, EditDeleteHead);
			break; case LNext:  e->vi.lnext = true; return 0;
			break; case WErase: return editFn(e, EditDeletePrevWord);
			break; case Esc: {
				viEscape(e);
				if (e->pos) e->pos--;
				return 0;
			}
		}
	}
	e->vi.lnext = false;
	if (e->vi.verb == 'R' && e->pos < e->len) {
		if (!isvalid(ch)) return -1;
		e->buf[e->pos] = ch;
		e->pos++;
	} else if (e->vi.verb == 'r' && e->pos < e->len) {
		if (!isvalid(ch)) return -1;
		size_t pos = e->pos;
		for (unsigned i = 0; i < (e->vi.count ?: 1); ++i) {
			if (e->pos + i == e->len) break;
			e->buf[(pos = e->pos + i)] = ch;
		}
		e->pos = pos;
		viEscape(e);
	} else {
		return editInsert(e, ch);
	}
	return 0;
}

static int viCommand(struct Edit *e, wchar_t ch) {
	if ((ch >= L'1' && ch <= L'9') || (e->vi.count && ch == L'0')) {
		e->vi.count *= 10;
		e->vi.count += ch - L'0';
		return 0;
	}
	switch (ch) {
		break; case Esc: viEscape(e);
		break; case L'0': e->pos = 0; viEscape(e);
		break; case L'R': e->vi.verb = 'R'; e->vi.mode = EditViInsert;
		break; case L'i': e->vi.verb = 'i'; e->vi.mode = EditViInsert;
		break; case L'r': e->vi.verb = 'r'; e->vi.mode = EditViInsert;
	}
	return 0;
}

int editVi(struct Edit *e, wchar_t ch) {
	switch (e->vi.mode) {
		break; case EditViInsert: return viInsert(e, ch);
		break; case EditViCommand: return viCommand(e, ch);
	}
}

#ifdef TEST
#undef NDEBUG
#include <assert.h>
#include <string.h>

static void fix(struct Edit *e, const char *str) {
	e->vi.mode = EditViInsert;
	assert(0 == editFn(e, EditClear));
	for (const char *ch = str; *ch; ++ch) {
		assert(0 == editInsert(e, (wchar_t)*ch));
	}
}

static bool eq(struct Edit *e, const char *str1) {
	size_t pos;
	static size_t cap;
	static char *buf;
	assert(NULL != editString(e, &buf, &cap, &pos));
	const char *str2 = &str1[strlen(str1) + 1];
	return pos == strlen(str1)
		&& !strncmp(buf, str1, pos)
		&& !strcmp(&buf[pos], str2);
}

#define editFn(...) assert(0 == editFn(__VA_ARGS__))
#define editVi(...) assert(0 == editVi(__VA_ARGS__))

static void vi(struct Edit *e, const char *str) {
	for (const char *ch = str; *ch; ++ch) {
		editVi(e, *ch);
	}
}

int main(void) {
	struct Edit cut = {0};
	struct Edit e = { .cut = &cut };

	fix(&e, "foo bar");
	editFn(&e, EditHead);
	assert(eq(&e, "\0foo bar"));
	editFn(&e, EditTail);
	assert(eq(&e, "foo bar\0"));
	editFn(&e, EditPrev);
	assert(eq(&e, "foo ba\0r"));
	editFn(&e, EditNext);
	assert(eq(&e, "foo bar\0"));

	fix(&e, "foo, bar");
	editFn(&e, EditPrevWord);
	assert(eq(&e, "foo, \0bar"));
	editFn(&e, EditPrevWord);
	assert(eq(&e, "\0foo, bar"));
	editFn(&e, EditNextWord);
	assert(eq(&e, "foo, \0bar"));
	editFn(&e, EditNextWord);
	assert(eq(&e, "foo, bar\0"));

	fix(&e, "foo bar");
	editFn(&e, EditPrevWord);
	editFn(&e, EditDeleteHead);
	assert(eq(&e, "\0bar"));

	fix(&e, "foo bar");
	editFn(&e, EditPrevWord);
	editFn(&e, EditDeleteTail);
	assert(eq(&e, "foo \0"));

	fix(&e, "foo bar");
	editFn(&e, EditDeletePrev);
	assert(eq(&e, "foo ba\0"));
	editFn(&e, EditHead);
	editFn(&e, EditDeleteNext);
	assert(eq(&e, "\0oo ba"));

	fix(&e, "foo, bar");
	editFn(&e, EditDeletePrevWord);
	assert(eq(&e, "foo, \0"));
	editFn(&e, EditDeletePrevWord);
	assert(eq(&e, "\0"));

	fix(&e, "foo, bar");
	editFn(&e, EditHead);
	editFn(&e, EditDeleteNextWord);
	assert(eq(&e, "\0, bar"));
	editFn(&e, EditDeleteNextWord);
	assert(eq(&e, "\0"));

	fix(&e, "foo bar");
	editFn(&e, EditDeletePrevWord);
	editFn(&e, EditPaste);
	assert(eq(&e, "foo bar\0"));
	editFn(&e, EditPaste);
	assert(eq(&e, "foo barbar\0"));

	fix(&e, "bar");
	editFn(&e, EditTranspose);
	assert(eq(&e, "bra\0"));
	editFn(&e, EditHead);
	editFn(&e, EditTranspose);
	assert(eq(&e, "rb\0a"));
	editFn(&e, EditTranspose);
	assert(eq(&e, "rab\0"));

	fix(&e, "  foo  bar  ");
	editFn(&e, EditCollapse);
	assert(eq(&e, "foo bar\0"));

	fix(&e, "foo");
	assert(e.vi.mode == EditViInsert);
	editVi(&e, Esc);
	assert(e.vi.mode == EditViCommand);
	assert(eq(&e, "fo\0o"));
	editVi(&e, L'i');
	assert(e.vi.mode == EditViInsert);
	editVi(&e, L'b');
	assert(eq(&e, "fob\0o"));
	editVi(&e, Esc);
	assert(eq(&e, "fo\0bo"));

	fix(&e, "foo bar");
	editVi(&e, Erase);
	assert(eq(&e, "foo ba\0"));
	editVi(&e, WErase);
	assert(eq(&e, "foo \0"));
	editVi(&e, LNext);
	editVi(&e, Esc);
	assert(eq(&e, "foo \33\0"));
	editVi(&e, Kill);
	assert(eq(&e, "\0"));

	fix(&e, "foo");
	vi(&e, "\0330Rba");
	assert(eq(&e, "ba\0o"));
	vi(&e, "r baz");
	assert(eq(&e, "bar baz\0"));

	fix(&e, "foo");
	vi(&e, "\0330rx");
	assert(eq(&e, "\0xoo"));
	vi(&e, "2ry");
	assert(eq(&e, "y\0yo"));
	vi(&e, "3rz");
	assert(eq(&e, "yz\0z"));
}

#endif /* TEST */
