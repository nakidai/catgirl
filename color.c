/* Copyright (C) 2019  C. McEnroe <june@causal.agency>
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

#include <stdint.h>

#include "chat.h"

// Adapted from <https://github.com/cbreeden/fxhash/blob/master/lib.rs>.
static uint32_t hashChar(uint32_t hash, char ch) {
	hash = (hash << 5) | (hash >> 27);
	hash ^= ch;
	hash *= 0x27220A95;
	return hash;
}

enum IRCColor colorGen(const char *str) {
	if (!str) return IRCDefault;
	uint32_t hash = 0;
	for (; str[0]; ++str) {
		hash = hashChar(hash, str[0]);
	}
	while (IRCBlack == (hash & IRCLightGray)) {
		hash = hashChar(hash, '\0');
	}
	return (hash & IRCLightGray);
}

static enum IRCColor colors[TagsLen];

struct Tag colorTag(struct Tag tag, const char *gen) {
	if (!colors[tag.id]) colors[tag.id] = 1 + colorGen(gen);
	return tag;
}

enum IRCColor colorFor(struct Tag tag) {
	return colors[tag.id] ? colors[tag.id] - 1 : IRCDefault;
}
