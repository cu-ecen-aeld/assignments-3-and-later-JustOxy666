DBGFLAGS := -g -Wall
PARAMS:=-lpthread
SRC := aesdsocket.c
OBJ := aesdsocket

default:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(DBGFLAGS) -o $(OBJ) $(SRC)
all:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(DBGFLAGS) -o $(OBJ) $(SRC)

clean:
	rm -f *.o aesdsocket
