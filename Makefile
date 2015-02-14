OBJECTS:=pidns.o

all: pidns

pidns: $(OBJECTS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%: %.o
	$(CC) $(LDFLAGS) -o $@ $<

clean:
	$(RM) pidns $(OBJECTS)
