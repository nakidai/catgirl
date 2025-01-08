PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANDIR ?= ${PREFIX}/man

CEXTS = gnu-case-range gnu-conditional-omitted-operand
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wmissing-prototypes
CFLAGS += ${CEXTS:%=-Wno-%}
LDADD.libtls = -ltls
LDADD.ncursesw = -lncursesw

BINS = catgirl
MANS = ${BINS:=.1}

-include config.mk

LDLIBS = ${LDADD.libtls} ${LDADD.ncursesw}
LDLIBS.sandman = -framework Cocoa

OBJS += buffer.o
OBJS += chat.o
OBJS += command.o
OBJS += complete.o
OBJS += config.o
OBJS += edit.o
OBJS += filter.o
OBJS += handle.o
OBJS += input.o
OBJS += irc.o
OBJS += log.o
OBJS += tr2cyr.o
OBJS += ui.o
OBJS += url.o
OBJS += window.o
OBJS += xdg.o

OBJS.sandman = sandman.o

TESTS += edit.t

dev: tags all check

all: ${BINS}

catgirl: ${OBJS}
	${CC} ${LDFLAGS} ${OBJS} ${LDLIBS} -o $@

${OBJS}: chat.h

edit.o edit.t input.o: edit.h

sandman: ${OBJS.sandman}
	${CC} ${LDFLAGS} ${OBJS.$@} ${LDLIBS.$@} -o $@

check: ${TESTS}

.SUFFIXES: .t

.c.t:
	${CC} ${CFLAGS} -DTEST ${LDFLAGS} $< ${LDLIBS} -o $@
	./$@ || rm $@

tags: *.[ch]
	ctags -w *.[ch]

README: README.7
	mandoc -Tascii $@.7 | col -b > $@

clean:
	rm -f ${BINS} ${OBJS} ${OBJS.sandman} ${TESTS} tags

install: ${BINS} ${MANS}
	install -d ${DESTDIR}${BINDIR} ${DESTDIR}${MANDIR}/man1
	install ${BINS} ${DESTDIR}${BINDIR}
	install -m 644 ${MANS} ${DESTDIR}${MANDIR}/man1

uninstall:
	rm -f ${BINS:%=${DESTDIR}${BINDIR}/%}
	rm -f ${MANS:%=${DESTDIR}${MANDIR}/man1/%}
