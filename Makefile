CC=gcc
CFLAGS=-g -c -O2 -Wall -Wpedantic -std=c11 -march=native
LDFLAGS=-lSDL2
SOURCES=main.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=test.out

all: $(SOURCES) $(EXECUTABLE)
    
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm $(OBJECTS)
