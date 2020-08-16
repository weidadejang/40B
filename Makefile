
TARGET=spi_pro
CFLAGS:=-g -O0 -W -Wall -Iinclude -std=c99 -D_GNU_SOURCE -DDAEMON
#CFLAGS:=-g -O0 -W -Wall -Iinclude -std=c99 -D_GNU_SOURCE
LDFLAGS:=
LIBS:= -lpthread -lm -lsqlite3
#LIBS += /usr/local/sqlite3/lib/libsqlite3.so
#LIBS += /usr/local/sqlite3/lib/libsqlite3.so.0
#LIBS += /usr/local/sqlite3/lib/libsqlite3.so.0.8.6
#CC:=arm-arago-linux-gnueabi-gcc
#CC:=arm-fsl-linux-gnueabi-gcc
#CC:=$(shell which gcc)

DBSRC=$(shell ls db/*.c)
UTILSRC=$(shell ls utils/*.c)
MAINSRC=main.c netThread.c debugThread.c up_packet.c serialThread.c gpioThread.c

SOURCES=${DBSRC} ${UTILSRC} ${MAINSRC}
OBJS=${SOURCES:.c=.o}

${TARGET}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS} ${LDFLAGS}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

.PHONY:clean

clean:
	@rm -rf ${OBJS} ${TARGET}
