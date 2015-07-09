CGC_LDFLAGS += -lcgc -nostdlib -L/usr/lib 
CGC_CFLAGS  += -fno-builtin -I/usr/include

all: $(ELF_BINS) $(CGC_BINS)
	@echo ELF binaries: $(ELF_BINS)
	@echo CGC binaries: $(CGC_BINS)

$(CGC_BINS):
	PATH=/usr/i386-linux-cgc/bin:$(PATH) $(CC) $(CGC_CFLAGS) -c $@.c -o $@.o
	PATH=/usr/i386-linux-cgc/bin:$(PATH) $(LD) $(CGC_LDFLAGS) $@.o -o $@

$(ELF_BINS):
	$(CC) $(CFLAGS) $@.c -o $@

.PHONY: test

test: $(CGC_TESTS)

$(CGC_TESTS):
	python $@.py

clean:
	rm -f $(ELF_BINS) $(CGC_BINS) *.o


