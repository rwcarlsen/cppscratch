
#CXXFLAGS := -g -ltcmalloc_and_profiler -O2
CXXFLAGS := -g -O3
main: main.cc show.h graph.h builder.h
	clang++ $(CXXFLAGS) -std=c++11 -o $@ $<

clean:
	rm -f main

