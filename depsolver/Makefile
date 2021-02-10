
#CXXFLAGS := -g -ltcmalloc_and_profiler -O2
CXXFLAGS := -g# -O3
main: main.cc show.cc graph.cc graph.h show.h builder.h
	clang++ $(CXXFLAGS) -std=c++11 -o $@ main.cc show.cc graph.cc

clean:
	rm -f main

