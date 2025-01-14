/* Copyright (C) 2020  June McEnroe <june@causal.agency>
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

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
#define BIT(x) x##Bit, x = 1 << x##Bit, x##Bit_ = x##Bit

typedef unsigned uint;
typedef unsigned char byte;

static inline char *seprintf(char *ptr, char *end, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
static inline char *seprintf(char *ptr, char *end, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(ptr, end - ptr, fmt, ap);
	va_end(ap);
	if (n < 0) return NULL;
	if (n > end - ptr) return end;
	return ptr + n;
}

enum Attr {
	BIT(Bold),
	BIT(Reverse),
	BIT(Italic),
	BIT(Underline),
};
enum Color {
	White, Black, Blue, Green, Red, Brown, Magenta, Orange,
	Yellow, LightGreen, Cyan, LightCyan, LightBlue, Pink, Gray, LightGray,
	Default = 99,
	ColorCap,
};
struct Style {
	enum Attr attr;
	enum Color fg, bg;
};

static const struct Style StyleDefault = { 0, Default, Default };
enum { B = '\2', C = '\3', O = '\17', R = '\26', I = '\35', U = '\37' };

static inline size_t styleParse(struct Style *style, const char **str) {
	switch (**str) {
		break; case B: (*str)++; style->attr ^= Bold;
		break; case O: (*str)++; *style = StyleDefault;
		break; case R: (*str)++; style->attr ^= Reverse;
		break; case I: (*str)++; style->attr ^= Italic;
		break; case U: (*str)++; style->attr ^= Underline;
		break; case C: {
			(*str)++;
			if (!isdigit(**str)) {
				style->fg = Default;
				style->bg = Default;
				break;
			}
			style->fg = *(*str)++ - '0';
			if (isdigit(**str)) style->fg = style->fg * 10 + *(*str)++ - '0';
			if ((*str)[0] != ',' || !isdigit((*str)[1])) break;
			(*str)++;
			style->bg = *(*str)++ - '0';
			if (isdigit(**str)) style->bg = style->bg * 10 + *(*str)++ - '0';
		}
	}
	return strcspn(*str, (const char[]) { B, C, O, R, I, U, '\0' });
}

static inline void styleStrip(char *buf, size_t cap, const char *str) {
	*buf = '\0';
	char *ptr = buf, *end = &buf[cap];
	struct Style style = StyleDefault;
	while (*str) {
		size_t len = styleParse(&style, &str);
		ptr = seprintf(ptr, end, "%.*s", (int)len, str);
		str += len;
	}
}

enum { None, Debug, Network, IDCap = 256 };
extern char *idNames[IDCap];
extern enum Color idColors[IDCap];
extern uint idNext;

static inline uint idFind(const char *name) {
	for (uint id = 0; id < idNext; ++id) {
		if (!strcasecmp(idNames[id], name)) return id;
	}
	return None;
}

static inline uint idFor(const char *name) {
	uint id = idFind(name);
	if (id) return id;
	if (idNext == IDCap) return Network;
	idNames[idNext] = strdup(name);
	idColors[idNext] = Default;
	if (!idNames[idNext]) err(1, "strdup");
	return idNext++;
}

extern uint32_t hashInit;
extern uint32_t hashBound;
static inline uint32_t _hash(const char *str) {
	if (*str == '~') str++;
	uint32_t hash = hashInit;
	for (; *str; ++str) {
		hash = (hash << 5) | (hash >> 27);
		hash ^= *str;
		hash *= 0x27220A95;
	}
	return hash;
}
static inline enum Color hash(const char *str) {
	if (hashBound < Blue) return Default;
	return Blue + _hash(str) % (hashBound + 1 - Blue);
}

extern struct Network {
	char *name;
	uint userLen;
	uint hostLen;
	char *chanTypes;
	char *statusmsg;
	char *prefixes;
	char *prefixModes;
	char *listModes;
	char *paramModes;
	char *setParamModes;
	char *channelModes;
	char excepts;
	char invex;
} network;

static inline uint prefixBit(char p) {
	char *s = strchr(network.prefixes, p);
	if (!s) return 0;
	return 1 << (s - network.prefixes);
}

static inline char bitPrefix(uint p) {
	for (uint i = 0; network.prefixes[i]; ++i) {
		if (p & (1 << i)) return network.prefixes[i];
	}
	return '\0';
}

#define ENUM_CAP \
	X("causal.agency/consumer", CapConsumer) \
	X("chghost", CapChghost) \
	X("extended-join", CapExtendedJoin) \
	X("invite-notify", CapInviteNotify) \
	X("message-tags", CapMessageTags) \
	X("multi-prefix", CapMultiPrefix) \
	X("sasl", CapSASL) \
	X("server-time", CapServerTime) \
	X("setname", CapSetname) \
	X("userhost-in-names", CapUserhostInNames) \
	X("znc.in/self-message", CapSelfMessage)

enum Cap {
#define X(name, id) BIT(id),
	ENUM_CAP
#undef X
};

extern struct Self {
	bool debug;
	bool restricted;
	size_t pos;
	enum Cap caps;
	const char *plainUser;
	char *plainPass;
	const char *nicks[8];
	const char *mode;
	const char *join;
	char *nick;
	char *user;
	char *host;
	enum Color color;
	char *invited;
	char *quit;
} self;

static inline void set(char **field, const char *value) {
	free(*field);
	*field = strdup(value);
	if (!*field) err(1, "strdup");
}

#define ENUM_TAG \
	X("+draft/reply", TagReply) \
	X("causal.agency/pos", TagPos) \
	X("msgid", TagMsgID) \
	X("time", TagTime)

enum Tag {
#define X(name, id) id,
	ENUM_TAG
#undef X
	TagCap,
};

enum { ParamCap = 254 };
struct Message {
	char *tags[TagCap];
	char *nick;
	char *user;
	char *host;
	char *cmd;
	char *params[ParamCap];
};

void ircConfig(
	bool insecure, const char *trust, const char *cert, const char *priv
);
int ircConnect(const char *bind, const char *host, const char *port);
void ircHandshake(void);
void ircPrintCert(void);
void ircRecv(void);
void ircSend(const char *ptr, size_t len);
void ircFormat(const char *format, ...)
	__attribute__((format(printf, 1, 2)));
void ircClose(void);

extern uint execID;
extern int execPipe[2];
extern int utilPipe[2];

enum { UtilCap = 16 };
struct Util {
	uint argc;
	const char *argv[UtilCap];
};

static inline void utilPush(struct Util *util, const char *arg) {
	if (1 + util->argc < UtilCap) {
		util->argv[util->argc++] = arg;
	} else {
		errx(1, "too many utility arguments");
	}
}

enum Reply {
	ReplyAway = 1,
	ReplyBan,
	ReplyExcepts,
	ReplyHelp,
	ReplyInvex,
	ReplyJoin,
	ReplyList,
	ReplyMode,
	ReplyNames,
	ReplyNamesAuto,
	ReplyTopic,
	ReplyTopicAuto,
	ReplyWhois,
	ReplyWhowas,
	ReplyCap,
};

extern uint replies[ReplyCap];

void handle(struct Message *msg);
void command(uint id, char *input);
const char *commandIsPrivmsg(uint id, const char *input);
const char *commandIsNotice(uint id, const char *input);
const char *commandIsAction(uint id, const char *input);
size_t commandWillSplit(uint id, const char *input);
void commandCompletion(void);

enum Heat {
	Ice,
	Cold,
	Warm,
	Hot,
};

enum {
	TitleCap = 256,
	StatusLines = 1,
	MarkerLines = 1,
	SplitLines = 5,
	InputLines = 1,
	InputCols = 1024,
};
extern char uiTitle[TitleCap];
extern struct _win_st *uiStatus;
extern struct _win_st *uiMain;
extern struct _win_st *uiInput;
extern bool uiSpoilerReveal;
extern struct Util uiNotifyUtil;
void uiInit(void);
uint uiAttr(struct Style style);
short uiPair(struct Style style);
void uiShow(void);
void uiHide(void);
void uiDraw(void);
void uiResize(void);
void uiWrite(uint id, enum Heat heat, const time_t *time, const char *str);
void uiFormat(
	uint id, enum Heat heat, const time_t *time, const char *format, ...
) __attribute__((format(printf, 4, 5)));
void uiLoad(const char *name);
int uiSave(void);

extern enum InputMode {
	InputEmacs,
	InputVi,
} inputMode;
void inputInit(void);
void inputWait(void);
void inputUpdate(void);
bool inputPending(uint id);
void inputRead(void);
void inputCompletion(void);
int inputSave(FILE *file);
void inputLoad(FILE *file, size_t version);

enum Scroll {
	ScrollOne,
	ScrollPage,
	ScrollAll,
	ScrollUnread,
	ScrollHot,
};
extern struct Time {
	bool enable;
	const char *format;
	int width;
} windowTime;
extern enum Heat windowThreshold;
void windowInit(void);
void windowUpdate(void);
void windowResize(void);
bool windowWrite(uint id, enum Heat heat, const time_t *time, const char *str);
void windowBare(void);
uint windowID(void);
uint windowNum(void);
uint windowFor(uint id);
void windowShow(uint num);
void windowAuto(void);
void windowSwap(void);
void windowMove(uint from, uint to);
void windowClose(uint num);
void windowList(void);
void windowMark(void);
void windowUnmark(void);
void windowToggleMute(void);
void windowToggleTime(void);
void windowToggleThresh(int n);
bool windowTimeEnable(void);
void windowScroll(enum Scroll by, int n);
void windowSearch(const char *str, int dir);
int windowSave(FILE *file);
void windowLoad(FILE *file, size_t version);

enum { BufferCap = 1024 };
struct Buffer;
struct Line {
	uint num;
	enum Heat heat;
	time_t time;
	char *str;
};
struct Buffer *bufferAlloc(void);
void bufferFree(struct Buffer *buffer);
const struct Line *bufferSoft(const struct Buffer *buffer, size_t i);
const struct Line *bufferHard(const struct Buffer *buffer, size_t i);
int bufferPush(
	struct Buffer *buffer, int cols, enum Heat thresh,
	enum Heat heat, time_t time, const char *str
);
int bufferReflow(
	struct Buffer *buffer, int cols, enum Heat thresh, size_t tail
);

struct Cursor {
	uint gen;
	struct Node *node;
};
void completePush(uint id, const char *str, enum Color color);
void completePull(uint id, const char *str, enum Color color);
void completeReplace(const char *old, const char *new);
void completeRemove(uint id, const char *str);
enum Color completeColor(uint id, const char *str);
uint *completeBits(uint id, const char *str);
const char *completePrefix(struct Cursor *curs, uint id, const char *prefix);
const char *completeSubstr(struct Cursor *curs, uint id, const char *substr);
const char *completeEach(struct Cursor *curs, uint id);
uint completeEachID(struct Cursor *curs, const char *str);
void completeAccept(struct Cursor *curs);
void completeReject(struct Cursor *curs);

extern struct Util urlOpenUtil;
extern struct Util urlCopyUtil;
void urlScan(uint id, const char *nick, const char *mesg);
void urlOpenCount(uint id, uint count);
void urlOpenMatch(uint id, const char *str);
void urlCopyMatch(uint id, const char *str);
int urlSave(FILE *file);
void urlLoad(FILE *file, size_t version);

enum { FilterCap = 64 };
extern struct Filter {
	enum Heat heat;
	char *mask;
	char *cmd;
	char *chan;
	char *mesg;
} filters[FilterCap];
struct Filter filterParse(enum Heat heat, char *pattern);
struct Filter filterAdd(enum Heat heat, const char *pattern);
bool filterRemove(struct Filter filter);
enum Heat filterCheck(enum Heat heat, uint id, const struct Message *msg);

void logOpen(void);
void logFormat(uint id, const time_t *time, const char *format, ...)
	__attribute__((format(printf, 3, 4)));
void logClose(void);

char *configPath(char *buf, size_t cap, const char *path, int i);
char *dataPath(char *buf, size_t cap, const char *path, int i);
FILE *configOpen(const char *path, const char *mode);
FILE *dataOpen(const char *path, const char *mode);

int getopt_config(
	int argc, char *const *argv,
	const char *optstring, const struct option *longopts, int *longindex
);
