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
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include "chat.h"

struct Node {
	uint id;
	char *str;
	enum Color color;
	struct Node *prev;
	struct Node *next;
};

static struct Node *alloc(uint id, const char *str, enum Color color) {
	struct Node *node = malloc(sizeof(*node));
	if (!node) err(EX_OSERR, "malloc");
	node->id = id;
	node->str = strdup(str);
	node->color = color;
	node->prev = NULL;
	node->next = NULL;
	if (!node->str) err(EX_OSERR, "strdup");
	return node;
}

static struct Node *head;
static struct Node *tail;

static struct Node *detach(struct Node *node) {
	if (node->prev) node->prev->next = node->next;
	if (node->next) node->next->prev = node->prev;
	if (head == node) head = node->next;
	if (tail == node) tail = node->prev;
	node->prev = NULL;
	node->next = NULL;
	return node;
}

static struct Node *prepend(struct Node *node) {
	node->prev = NULL;
	node->next = head;
	if (head) head->prev = node;
	head = node;
	tail = (tail ?: node);
	return node;
}

static struct Node *append(struct Node *node) {
	node->next = NULL;
	node->prev = tail;
	if (tail) tail->next = node;
	tail = node;
	head = (head ?: node);
	return node;
}

static struct Node *find(uint id, const char *str) {
	for (struct Node *node = head; node; node = node->next) {
		if (node->id != id) continue;
		if (strcmp(node->str, str)) continue;
		return node;
	}
	return NULL;
}

void completeAdd(uint id, const char *str, enum Color color) {
	if (!find(id, str)) append(alloc(id, str, color));
}

void completeTouch(uint id, const char *str, enum Color color) {
	struct Node *node = find(id, str);
	if (node) node->color = color;
	prepend(node ? detach(node) : alloc(id, str, color));
}

enum Color completeColor(uint id, const char *str) {
	struct Node *node = find(id, str);
	return (node ? node->color : Default);
}

static struct Node *match;

const char *complete(uint id, const char *prefix) {
	for (match = (match ? match->next : head); match; match = match->next) {
		if (match->id && match->id != id) continue;
		if (strncasecmp(match->str, prefix, strlen(prefix))) continue;
		return match->str;
	}
	return NULL;
}

void completeAccept(void) {
	if (match) prepend(detach(match));
	match = NULL;
}

void completeReject(void) {
	match = NULL;
}

uint completeID(const char *str) {
	for (match = (match ? match->next : head); match; match = match->next) {
		if (match->id && !strcmp(match->str, str)) return match->id;
	}
	return None;
}

void completeReplace(uint id, const char *old, const char *new) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (id && node->id != id) continue;
		if (strcmp(node->str, old)) continue;
		free(node->str);
		node->str = strdup(new);
		prepend(detach(node));
		if (!node->str) err(EX_OSERR, "strdup");
	}
}

void completeRemove(uint id, const char *str) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (id && node->id != id) continue;
		if (strcmp(node->str, str)) continue;
		if (match == node) match = NULL;
		detach(node);
		free(node->str);
		free(node);
	}
}

void completeClear(uint id) {
	struct Node *next = NULL;
	for (struct Node *node = head; node; node = next) {
		next = node->next;
		if (node->id != id) continue;
		if (match == node) match = NULL;
		detach(node);
		free(node->str);
		free(node);
	}
}
