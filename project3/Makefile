CC= gcc
CFLAGS= -Wall -O -g
SRCS= e2undel.c
OBJS= e2undel.o
PROGRAMS= e2undel
MKDEP= gccmakedep

e2undel: e2undel.o
	${CC} ${CFLAGS} e2undel.o -o e2undel

all: e2undel

%.o : %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f ${OBJS} ${PROGRAMS}

depend:
	${MKDEP} ${CFLAGS} ${SRCS}
