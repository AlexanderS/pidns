OBJECTS:=pidns.o
DEFAULT_CFLAGS:=-Wall -Werror -D_GNU_SOURCE

all: pidns

pidns: $(OBJECTS)

%.o: %.c
	$(CC) -c $(DEFAULT_CFLAGS) $(CFLAGS) -o $@ $<

%: %.o
	$(CC) $(LDFLAGS) -o $@ $<

clean:
	$(RM) pidns $(OBJECTS)
