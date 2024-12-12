DBGFLAGS ?= -g -Wall
LDFLAGS ?=-lpthread
SRC ?= aesdsocket.c
OBJ ?= aesdsocket

default:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) -o $(OBJ) $(SRC)
all:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) -o $(OBJ) $(SRC)

clean:
	rm -f *.o aesdsocket
