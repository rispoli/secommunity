CC=/usr/bin/g++
CFLAGS=-W -Wall -Wextra -Werror -pedantic
LDFLAGS=-lboost_program_options-mt
SOURCES:=$(wildcard *.cpp)
BOOST=/usr/include/boost
EXECUTABLE=dlv-server

all: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCES) message.h
	$(CC) $(CFLAGS) $(LDFLAGS) -I $(BOOST) $(SOURCES) -o $@

clean:
	-rm -f $(EXECUTABLE)
