OPTIMIZE = -g -O2
WARN=-W -Wall -Wno-c++98-compat -Wextra #-Weverything  #clang only! 
DEBUG =
CXXFLAGS = $(OPTIMIZE) $(DEBUG) -std=c++17 $(WARN)

# get the extra library for g++
LDFLAGS-g++ = -lstdc++fs
LDFLAGS = ${LDFLAGS-${CXX}}

# You may change the program name, by using
# CPPFLAGS = -DMYNAME=\"name\"

all: rr

clean:
	-rm -f rr rr.o

.PHONY: clean all
	
