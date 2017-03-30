
main: main.cc valuer.h moose.h
	clang++ -O2 -g -std=c++11 -o $@ $<

clean:
	rm -f main

