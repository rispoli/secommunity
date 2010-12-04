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
	$(CC) $(LDFLAGS) $(OBJECTS) $(ARGTABLEO) -o $@

$(EXECUTABLE)-dynamic: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE) $(LDLIBS)

# Can be cross-compiled with MinGW: s/CC/MINGW/ & appropriate argtable2
$(EXECUTABLE)-windows: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(ARGTABLEO) -o $(EXECUTABLE).exe -lws2_32

$(EXECUTABLE)-windows-dynamic: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE).exe -lws2_32 $(LDLIBS)

dlv-server.o: $(SOURCES) message.h
	$(CC) $(CFLAGS) -c $(ARGTABLEI) $< -o $@

clean:
	-rm -f $(OBJECTS) $(EXECUTABLE) $(EXECUTABLE).exe
