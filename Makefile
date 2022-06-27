# main: main.c
# 	gcc -Wall main.c -lpthread -o DelayDaemon -lm $(shell pkg-config --cflags libevdev)
TARGET = DelayDaemon

CFLAGS = -Wall -pedantic -O3 -std=gnu11 $(shell pkg-config --cflags libevdev)
LDFLAGS = $(shell pkg-config --libs libevdev)
LIBS = -pthread -lm

OBJECTS = \
	main.o

$(TARGET) : $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o : %.c
	$(CC) $(CFLAGS)  -o $@ -c $<

clean :
	rm -f $(TARGET) *.o