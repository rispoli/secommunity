CC=/usr/bin/g++
CFLAGS=-W -Wall -Wextra -Werror -pedantic -ansi
ARGTABLE_PATH=./argtable2
ARGTABLEI=-I$(ARGTABLE_PATH)/include
ARGTABLEL=$(ARGTABLE_PATH)/lib
ARGTABLEO=$(ARGTABLEL)/libargtable2.a
LDFLAGS=-L$(ARGTABLEL)
LDLIBS=-largtable2
SOURCES:=$(wildcard *.cpp)
OBJECTS:=$(SOURCES:.cpp=.o)
EXECUTABLE=dlv-server

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(ARGTABLEO) -o $(EXECUTABLE)

$(EXECUTABLE)-dynamic: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE) $(LDLIBS)

dlv-server.o: $(SOURCES) message.h
	$(CC) $(CFLAGS) -c $(ARGTABLEI) $< -o $@

clean:
	-rm -f $(OBJECTS) $(EXECUTABLE)
