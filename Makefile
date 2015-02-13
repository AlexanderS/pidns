all: pidns

%: %.c
	$(CC) -o $@ $<
