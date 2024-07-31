SRCS = $(wildcard app/*.c)
OBJS = $(SRCS:.c=.o)
EXECUTABLE = /tmp/codecrafters-build-http-server-c

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I./app/ -I/usr/include/
LDFLAGS = -lcurl -lz

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(OBJS) $(EXECUTABLE)

.PHONY: all clean
