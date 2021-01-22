
#include "graph.h"
#include "show.h"
#include "builder.h"

#include <iostream>
#include <sstream>

void
case1b()
{
  Graph graph;
  auto a = graph.create("a", false, false, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", false, false, LoopType());
  auto d = graph.create("d", false, false, LoopType());

  auto e = graph.create("e", true, true, LoopType(LoopCategory::Nodal));
  auto f = graph.create("f", false, false, LoopType(LoopCategory::Nodal));
  a->needs(b, c, d);
  b->needs(c);
  e->needs(b);
  f->needs(e);

  std::vector<Subgraph> partitions;
  auto loops = computeLoops(graph, partitions);
  //printLoops(loops);
  std::cout << dotGraph(graph);
}

void
case1()
{
  Graph graph;
  auto a = graph.create("a", false, false, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", false, false, LoopType());
  auto d = graph.create("d", false, false, LoopType());
  a->needs(b, c, d);
  b->needs(c);

  std::vector<Subgraph> partitions;
  auto loops = computeLoops(graph, partitions);
  printLoops(loops);
  std::cout << dotGraph(graph);
}

void
case2()
{
  Graph graph;
  auto a = graph.create("a", false, false, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", false, false, LoopType());
  auto d = graph.create("d", false, false, LoopType());
  auto e = graph.create("e", true, true, LoopType());
  auto f = graph.create("f", true, true, LoopType());
  auto g = graph.create("g", true, true, LoopType());
  auto h = graph.create("h", false, false, LoopType());
  auto k = graph.create("k", false, false, LoopType());
  k->needs(f, g);
  f->needs(b);
  b->needs(a);
  g->needs(a);
  h->needs(e, d);
  e->needs(d);
  d->needs(c, b);

  std::vector<Subgraph> partitions;
  auto loops = computeLoops(graph, partitions);
  //printLoops(loops);
  std::cout << dotGraph(graph);
}

void
case3()
{
  Graph graph;
  auto a = graph.create("a", false, false, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", false, false, LoopType());
  auto d = graph.create("d", true, false, LoopType());
  auto e = graph.create("e", true, true, LoopType());
  auto f = graph.create("f", true, true, LoopType());
  auto g = graph.create("g", true, true, LoopType());
  auto h = graph.create("h", false, false, LoopType());
  auto k = graph.create("k", false, false, LoopType());
  k->needs(f, g);
  f->needs(b);
  b->needs(a);
  g->needs(a);
  h->needs(e, d);
  e->needs(d);
  d->needs(c, b);

  std::vector<Subgraph> partitions;
  auto loops = computeLoops(graph, partitions);
  printLoops(loops);
  std::cout << dotGraphMerged(partitions);
}

int
main(int narg, char ** argv)
{
  //std::cout << "::::: CASE 1  :::::\n";
  //case1();
  //std::cout << "::::: CASE 1b  :::::\n";
  //case1b();
  //std::cout << "::::: CASE 2  :::::\n";
  //case2();
  //std::cout << "::::: CASE 3  :::::\n";
  //case3();

  TransitionMatrix m;
  auto start_node = buildTransitionMatrix(m);
  int n_walks = 5;
  buildGraph(m, start_node, n_walks);

  std::vector<Subgraph> partitions;
  auto loops = computeLoops(m.graph, partitions);
  std::vector<Subgraph> filtered_partitions;
  for (auto & g : partitions)
    if (g.reachable({start_node}))
      filtered_partitions.push_back(g);
  std::cout << dotGraphMerged(filtered_partitions);
  //Subgraph g = m.graph.reachableFrom(start_node);
  //std::cout << dotGraph(g);

  return 0;
}
