CC=/usr/bin/g++
CFLAGS=-W -Wall -Wextra -Werror -pedantic
LDFLAGS=-lboost_program_options-mt
SOURCES:=$(wildcard *.cpp)
OBJECTS:=$(SOURCES:.cpp=.o)
EXECUTABLE=dlv-server

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) message.h
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS) $(EXECUTABLE)
