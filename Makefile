CC=gcc
CFLAGS=-I.
DEPS = 
LIBS = -lpthread
OBJ = serialserver.o 

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

serialserver: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJ) serialserver

