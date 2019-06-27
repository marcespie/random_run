OPTIMIZE = -O2
WARN=-W -Wall -Wno-c++98-compat -Wextra #-Weverything  #clang only! 
DEBUG =
CXXFLAGS = $(OPTIMIZE) $(DEBUG) -std=c++17 $(WARN)

# get the extra library for g++
LDFLAGS-g++ = -lstdc++fs
LDFLAGS = ${LDFLAGS-${CXX}}

prefix = /usr/local
DESTDIR =

# You may change the program name, by using
# CPPFLAGS = -DMYNAME=\"name\"

all: rr

install: 
	install -c -s -m 755 rr $(DESTDIR)${prefix}/bin
	install -c -m 644 rr.1 $(DESTDIR)${prefix}/man/man1

clean:
	-rm -f rr rr.o

.PHONY: clean all install
