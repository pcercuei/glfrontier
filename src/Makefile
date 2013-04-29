
SDLLIB = $(shell sdl-config --libs) $(LIBS)
SDLFLAGS = $(shell sdl-config --cflags) -L/usr/X11R6/include

SRCS = audio.c hostcall.c keymap.c main.c screen.c shortcut.c input.c

OBJS = $(SRCS:.c=.o)

.c.o:
	$(CC) $(CFLAGS) $(INCFLAGS) $(SDLFLAGS) -c $<

../frontier: $(OBJS)
	$(CC) $(OBJS) $(FE2OBJS) $(SDLLIB) $(LIBFLAGS) -o ../frontier

clean:
	rm -f *.o

# Use "make depends" to generate file dependencies:
depends: $(SRCS)
	$(CC) -MM $(CFLAGS) $(SRCS) > Makefile.dep

ifneq (,$(wildcard Makefile.dep))
include Makefile.dep
endif

