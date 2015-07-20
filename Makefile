TARGET=readcgcef
PYTARGET=readcgcef-minimal.py
OBJS=readcgcef.o
INCLUDES+=-I/usr/include -I/usr/include/libcgcef

LIBS=-L/usr/lib -lcgcef -lcgcdwarf

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) $(DEFINES) $(INCLUDES) $*.c -o $*.o

clean:
	rm -f $(TARGET) $(OBJS)

install:
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin
	install -m 755 $(PYTARGET) $(DESTDIR)/usr/bin
