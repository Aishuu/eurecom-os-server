NBC = ../../bin/nbc
T2N = ../../bin/t2n
TOP = client

all: $(TOP).rxe

%.rxe: %.nxc
	$(NBC) -O=$@ $<

.PHONY: install clean ultraclean

install: $(TOP).rxe
	sudo $(T2N) -put $<

clean:

ultraclean: clean
	rm -f $(TOP).rxe
