
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

  auto partitions = computePartitions(graph);
  auto loops = computeLoops(partitions);
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

  auto partitions = computePartitions(graph);
  auto loops = computeLoops(partitions);
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

  auto partitions = computePartitions(graph);
  auto loops = computeLoops(partitions);
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

  auto partitions = computePartitions(graph);
  auto loops = computeLoops(partitions);
  printLoops(loops);
  std::cout << dotGraphMerged(partitions);
}

// test the case where we have separate mesh loops that can be merged together
// all Elemental reducing nodes.
void
case4()
{
  //   a
  //   |\
  //   | \
  //   b  e
  //   |  |
  //   |  |
  //   c  f
  //   |  |
  //   |  |
  //   d  g
  Graph graph;
  auto a = graph.create("a", true, true, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", true, true, LoopType());
  auto d = graph.create("d", true, true, LoopType());
  auto e = graph.create("e", true, true, LoopType());
  auto f = graph.create("f", true, true, LoopType());
  auto g = graph.create("g", true, true, LoopType());

  g->needs(f);
  f->needs(e);
  e->needs(a);
  d->needs(c);
  c->needs(b);
  b->needs(a);

  auto partitions = computePartitions(graph, true);
  auto loops = computeLoops(partitions);
  printLoops(loops);
}

// test the case where we have separate mesh loops that can be merged together
// all Elemental reducing nodes except e and d are nodal reducing.  This is
// specifically checking that we detect the correct, "optimized" merges to do
// - where we don't want to merge e and d - because that would prevent us from
// doing two other merges (b,f and c,g) - resulting in more total
// loops/partitions.
void
case5a()
{
  //   a
  //   |\
  //   | \
  //   b  e
  //   |  |
  //   |  |
  //   c  f
  //   |  |
  //   |  |
  //   d  g
  Graph graph;
  auto a = graph.create("a", true, true, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", true, true, LoopType());
  auto d = graph.create("d", true, true, LoopType(LoopCategory::Nodal));
  auto e = graph.create("e", true, true, LoopType(LoopCategory::Nodal));
  auto f = graph.create("f", true, true, LoopType());
  auto g = graph.create("g", true, true, LoopType());

  g->needs(f);
  f->needs(e);
  e->needs(a);
  d->needs(c);
  c->needs(b);
  b->needs(a);

  auto partitions = computePartitions(graph, true);
  auto loops = computeLoops(partitions);
  printLoops(loops);
}

// same as 5a but b and g are nodal instead of e and d.
void
case5b()
{
  //   a
  //   |\
  //   | \
  //   b  e
  //   |  |
  //   |  |
  //   c  f
  //   |  |
  //   |  |
  //   d  g
  Graph graph;
  auto a = graph.create("a", true, true, LoopType());
  auto b = graph.create("b", true, true, LoopType(LoopCategory::Nodal));
  auto c = graph.create("c", true, true, LoopType());
  auto d = graph.create("d", true, true, LoopType());
  auto e = graph.create("e", true, true, LoopType());
  auto f = graph.create("f", true, true, LoopType());
  auto g = graph.create("g", true, true, LoopType(LoopCategory::Nodal));

  g->needs(f);
  f->needs(e);
  e->needs(a);
  d->needs(c);
  c->needs(b);
  b->needs(a);

  auto partitions = computePartitions(graph, true);
  auto loops = computeLoops(partitions);
  printLoops(loops);
}

// everything depends on a.  This makes sure that multiple merge-pairs into
// a single, accumulating subgraph are all consolidated/merged correctly.
void
case6()
{
  //    b----a-----f
  //        /|\
  //       / | \
  //      c  d  e
  Graph graph;
  auto a = graph.create("a", true, true, LoopType());
  auto b = graph.create("b", true, true, LoopType());
  auto c = graph.create("c", true, true, LoopType());
  auto d = graph.create("d", true, true, LoopType());
  auto e = graph.create("e", true, true, LoopType());
  auto f = graph.create("f", true, true, LoopType());

  b->needs(a);
  c->needs(a);
  d->needs(a);
  e->needs(a);
  f->needs(a);

  auto partitions = computePartitions(graph, true);
  auto loops = computeLoops(partitions);
  printLoops(loops);
}
void caseAutogen1()
{
  int n_walks = 5;
  bool sync_blocks = true;
  TransitionMatrix m;
  auto start_node = buildTransitionMatrix(m);
  buildGraph(m, start_node, n_walks, sync_blocks);

  auto partitions = computePartitions(m.graph);
  auto loops = computeLoops(partitions);
  std::vector<Subgraph> filtered_partitions;
  for (auto & g : partitions)
    if (g.reachable({start_node}))
      filtered_partitions.push_back(g);
  mergeSiblings(filtered_partitions);
  std::cout << dotGraphMerged(filtered_partitions);
  //Subgraph g = m.graph.reachableFrom(start_node);
  //std::cout << dotGraph(g);
  //printLoops(loops);
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
  std::cout << "::::: CASE 4  :::::\n";
  case4();
  std::cout << "::::: CASE 5a  :::::\n";
  case5a();
  std::cout << "::::: CASE 5b  :::::\n";
  case5b();
  std::cout << "::::: CASE 6  :::::\n";
  case6();

  //caseAutogen1();

  return 0;
}
