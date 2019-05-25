OPTIMIZE = -O2
WARN=-W -Wall -Wno-c++98-compat -Wextra #-Weverything  #clang only! 
DEBUG =
CXXFLAGS = $(OPTIMIZE) $(DEBUG) -std=c++17 $(WARN)


all: rr

clean:
	-rm -f rr rr.o

.PHONY: clean all
	
