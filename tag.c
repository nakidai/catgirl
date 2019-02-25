/* Copyright (C) 2018  C. McEnroe <june@causal.agency>
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

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

static struct {
	char *name[TagsLen];
	enum IRCColor color[TagsLen];
	size_t len;
} tags = {
	.name = { "<none>", "<status>", "<raw>" },
	.color = { IRCBlack, IRCDefault, IRCRed },
	.len = 3,
};

const struct Tag TagNone   = { 0, "<none>", IRCBlack };
const struct Tag TagStatus = { 1, "<status>", IRCDefault };
const struct Tag TagRaw    = { 2, "<raw>", IRCRed };

struct Tag tagFind(const char *name) {
	for (size_t id = 0; id < tags.len; ++id) {
		if (strcmp(tags.name[id], name)) continue;
		return (struct Tag) { id, tags.name[id], tags.color[id] };
	}
	return TagNone;
}

struct Tag tagFor(const char *name, enum IRCColor color) {
	struct Tag tag = tagFind(name);
	if (tag.id != TagNone.id) {
		tag.color = tags.color[tag.id] = color;
		return tag;
	}
	if (tags.len == TagsLen) return TagStatus;

	size_t id = tags.len++;
	tags.name[id] = strdup(name);
	tags.color[id] = color;
	if (!tags.name[id]) err(EX_OSERR, "strdup");
	return (struct Tag) { id, tags.name[id], color };
}
