
TARGET=cmdproxy
CFLAGS:=-g -O0 -W -Wall -Iinclude -std=c99 -D_GNU_SOURCE -DDAEMON
#CFLAGS:=-g -O0 -W -Wall -Iinclude -std=c99 -D_GNU_SOURCE
#LDFLAGS:=-Wl,-wrap,free
LDFLAGS:=-W
LIBS:= -lpthread -lm -lsqlite3
#CC:=arm-arago-linux-gnueabi-gcc
#CC:=arm-fsl-linux-gnueabi-gcc
#CC:=$(shell which gcc)

DBSRC=$(shell ls db/*.c)
UTILSRC=$(shell ls utils/*.c)
MAINSRC=main.c netThread.c up_packet.c down_packet.c serialThread.c gpioThread.c debugThread.c

SOURCES=${DBSRC} ${UTILSRC} ${MAINSRC}
OBJS=${SOURCES:.c=.o}

${TARGET}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS} ${LDFLAGS}

%.o: %.c
	${CC} ${CFLAGS} -c $< -o $@

.PHONY:clean

clean:
	@rm -rf ${OBJS} ${TARGET}
