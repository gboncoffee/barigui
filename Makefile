CC = gcc
CFLAGS = -Wall -g
INCS = -I/usr/X11R6/include -I/usr/include/freetype2
LIBS = -L/usr/X11R6/lib
CLIBS = -lXft -lX11 -lfontconfig

all: barigui

barigui: barigui.c drw.h config.h
	$(CC) $(CFLAGS) $(INCS) $(LIBS) -o $@ barigui.c $(CLIBS)

clean:
	rm barigui
