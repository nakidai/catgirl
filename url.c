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

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "chat.h"

static const char *SCHEMES[] = {
	"https:",
	"http:",
	"ftp:",
};
static const size_t SCHEMES_LEN = sizeof(SCHEMES) / sizeof(SCHEMES[0]);

struct Entry {
	size_t tag;
	char *url;
};

enum { RING_LEN = 32 };
static_assert(!(RING_LEN & (RING_LEN - 1)), "power of two RING_LEN");

static struct {
	struct Entry buf[RING_LEN];
	size_t end;
} ring;

static void push(struct Tag tag, const char *url, size_t len) {
	free(ring.buf[ring.end].url);
	ring.buf[ring.end].tag = tag.id;
	ring.buf[ring.end].url = strndup(url, len);
	if (!ring.buf[ring.end].url) err(EX_OSERR, "strndup");
	ring.end = (ring.end + 1) & (RING_LEN - 1);
}

void urlScan(struct Tag tag, const char *str) {
	while (str[0]) {
		size_t len = 1;
		for (size_t i = 0; i < SCHEMES_LEN; ++i) {
			if (strncmp(str, SCHEMES[i], strlen(SCHEMES[i]))) continue;
			len = strcspn(str, " >\"");
			push(tag, str, len);
		}
		str = &str[len];
	}
}

void urlList(struct Tag tag) {
	uiHide();
	for (size_t i = 0; i < RING_LEN; ++i) {
		struct Entry entry = ring.buf[(ring.end + i) & (RING_LEN - 1)];
		if (!entry.url || entry.tag != tag.id) continue;
		printf("%s\n", entry.url);
	}
}

void urlOpen(struct Tag tag, size_t at, size_t to) {
	size_t argc = 1;
	char *argv[2 + RING_LEN] = { "open" };
	size_t tagIndex = 0;
	for (size_t i = 0; i < RING_LEN; ++i) {
		struct Entry entry = ring.buf[(ring.end - i) & (RING_LEN - 1)];
		if (!entry.url || entry.tag != tag.id) continue;
		if (tagIndex >= at && tagIndex < to) argv[argc++] = entry.url;
		tagIndex++;
	}
	if (argc > 1) spawn(argv);
}