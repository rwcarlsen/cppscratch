
main: main.cc valuer.h mock.h util.h
	clang++ -O2 -g -std=c++11 -o $@ $<

clean:
	rm -f main

