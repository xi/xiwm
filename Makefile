PREFIX = /usr
CFLAGS = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -Os
LDFLAGS = -lX11

SRC = xiwm.c
OBJ = ${SRC:.c=.o}

all: xiwm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h

config.h:
	cp config.def.h $@

xiwm: xiwm.o
	${CC} -o $@ $< ${LDFLAGS}

clean:
	rm -f xiwm ${OBJ}

install: all
	install -D -m 755 xiwm ${DESTDIR}${PREFIX}/bin/xiwm
	install -D -m 755 xiwm-session ${DESTDIR}${PREFIX}/bin/xiwm-session
	install -D xiwm.desktop ${DESTDIR}${PREFIX}/share/xsessions/xiwm.desktop

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/xiwm
	rm -f ${DESTDIR}${PREFIX}/bin/xiwm-session
	rm -f ${DESTDIR}${PREFIX}/share/xsessions/xiwm.desktop

.PHONY: all clean install uninstall
