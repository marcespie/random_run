OPTIMIZE = -O2
DEBUG =
CXXFLAGS = $(OPTIMIZE) $(DEBUG) -std=c++17 -W -Wall


all: rr

clean:
	-rm -f rr rr.o

.PHONY: clean all
	
