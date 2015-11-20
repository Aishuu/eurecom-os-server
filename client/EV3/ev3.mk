CC 		= gcc
CFLAGS 		= -Wall
LFLAGS 		= -lbluetooth
SRC_FILES 	= $(wildcard *.c)
OBJ_FILES	= $(SRC_FILES:%.c=%.o)
EXEC		= client
DEST_DIR	= /home/root

all: $(EXEC)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(EXEC): $(OBJ_FILES)
	$(CC) -o $@ $^ $(LFLAGS)

.PHONY: clean ultraclean install

clean:
	rm -f $(OBJ_FILES)
	rm -f $(EXEC)

ultraclean: clean
	rm -f $(DEST_DIR)/$(EXEC)

install: $(EXEC)
	cp $< $(DEST_DIR)
	chown robot:robot $(DEST_DIR)/$<
	chmod 755 $(DEST_DIR)/$<
