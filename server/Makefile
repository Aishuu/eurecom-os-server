CC 		= gcc
CFLAGS 		= -Wall
LFLAGS 		= -lbluetooth
SRC_FILES 	= $(wildcard *.c)
OBJ_FILES	= $(SRC_FILES:%.c=%.o)
EXEC		= server

all: $(EXEC)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(EXEC): $(OBJ_FILES)
	$(CC) -o $@ $^ $(LFLAGS)

.PHONY: clean ultraclean

clean:
	rm -f $(OBJ_FILES)

ultraclean: clean
	rm -f $(EXEC)
