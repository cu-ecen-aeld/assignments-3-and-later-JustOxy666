DBGFLAGS ?= -g -Wall
DBGBUILDFLAGS ?= -DDEBUG_ON
LDFLAGS ?=-lpthread
SRC ?= aesdsocket.c
OBJ ?= aesdsocket

default:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) -o $(OBJ) $(SRC)
all:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) -o $(OBJ) $(SRC)
debug:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) $(DBGBUILDFLAGS) -o $(OBJ) $(SRC)

clean:
	rm -f *.o aesdsocket
