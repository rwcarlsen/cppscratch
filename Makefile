
main: main.cc
	clang++ -O2 -std=c++11 -o $@ $^

clean:
	rm -f main

