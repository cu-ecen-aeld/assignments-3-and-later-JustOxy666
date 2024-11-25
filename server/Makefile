DBGFLAGS := -g -Wall
SRC := aesdsocket.c
OBJ := aesdsocket

default:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) -o $(OBJ) $(SRC)
all:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) -o $(OBJ) $(SRC)

clean:
	rm -f *.o aesdsocket
