CC=/usr/bin/g++
CFLAGS=-W -Wall -Wextra -Werror -pedantic
LDFLAGS=-lboost_program_options-mt
SOURCES:=$(wildcard *.cpp)
OBJECTS:=$(SOURCES:.cpp=.o)
BOOST=/usr/include/boost
EXECUTABLE=dlv-server

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o: message.h
	$(CC) $(CFLAGS) -I $(BOOST) -c $< -o $@

clean:
	-rm -f $(OBJECTS) $(EXECUTABLE)
