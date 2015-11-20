EV3_ADDR = 	10.42.0.3
EV3_DIR =	/root
MAKEFILE =	ev3.mk
SOURCES =	$(wildcard *.c)

.PHONY: install

all:
	@echo "make install             copy sources to EV3 directory"
	@echo "                         $(EV3_DIR) and compile them."

install: $(MAKEFILE)
	scp $(SOURCES) root@$(EV3_ADDR):$(EV3_DIR)
	scp $(MAKEFILE) root@$(EV3_ADDR):$(EV3_DIR)/Makefile
	ssh root@$(EV3_ADDR) 'cd $(EV3_DIR) && make && make install'
