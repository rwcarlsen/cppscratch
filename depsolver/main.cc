
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>

enum class LoopType
{
  None,
  Nodal,
  Face,
  Elemental,
};

// Nodes in the dependency graph have three properties that we track:
//
//     * stored vs not-stored: their computed value at mesh points is cached and does not need to be
//       recomputed across consecutive loops
//     * loop type: nodal, elemental, etc.
//     * reduction operation vs not: e.g. postprocessors perform a reducing operation.
//
// All reducing nodes store implicitly cache/store their values, but some non-reducing do as well.

class Node
{
public:
  Node(const std::string & name, bool cached, bool reducing, LoopType l = LoopType::Elemental)
    : _name(name), _cached(cached), _reducing(reducing), _looptype(l)
  {
  }
  int loop() const {
    if (_dependers.size() == 0)
      return 0;

    int maxloop = (*_dependers.begin())->loop();
    for (auto dep : _dependers)
      if (dep->loop() > maxloop)
        maxloop = dep->loop();

    if (isReducing())
      return maxloop + 1;
    return maxloop;
  }

  std::set<Node *> deps() const { return _deps; }
  std::set<Node *> dependers() const { return _dependers; }

  bool isReducing() const { return _reducing; }
  bool isCached() const { return _cached || _reducing; }
  LoopType loopType() const { return _looptype; }

  std::string str() { return _name; }

  void needs() {}

  template <typename... Args>
  void needs(Node * n, Args... args)
  {
    _deps.insert(n);
    n->_dependers.insert(this);
    needs(args...);
  }

private:
  std::string _name;
  bool _cached;
  bool _reducing;
  LoopType _looptype;
  std::set<Node *> _deps;
  std::set<Node *> _dependers;
};

class Subgraph
{
public:
  Subgraph() {}
  Subgraph(std::set<Node *> & nodes) : _nodes(nodes) {}
  virtual std::set<Node *> roots() const
  {
    std::set<Node *> rs;
    for (auto n : _nodes)
      if (filter(n->deps()).empty())
        rs.insert(n);
    return rs;
  }
  virtual std::set<Node *> leaves() const
  {
    std::set<Node *> leaves;
    for (auto n : _nodes)
      if (filter(n->dependers()).empty())
        leaves.insert(n);
    return leaves;
  }
  virtual void add(Node * n) { _nodes.insert(n); }
  virtual void remove(Node * n) { _nodes.erase(n); }
  virtual bool contains(Node * n) const { return _nodes.count(n) > 0; }
  virtual std::set<Node *> nodes() const { return _nodes; }

private:
  std::set<Node *> filter(std::set<Node *> ns) const
  {
    std::set<Node *> filtered;
    for (auto n : ns)
      if (contains(n))
        filtered.insert(n);
    return filtered;
  }
  std::set<Node *> _nodes;
};

class Graph : public Subgraph
{
public:
  template <typename... Args>
  Node * create(Args... args)
  {
    _node_storage.emplace_back(new Node(std::forward<Args>(args)...));
    add(_node_storage.back().get());
    return _node_storage.back().get();
  }

private:
  std::vector<std::unique_ptr<Node>> _node_storage;
};

// this effectively implements a topological sort for the nodes in the graph g. returning a list
// that can be executed that contains groups of nodes that can be run simultaneously.
void
execOrder(Subgraph g, std::vector<std::vector<Node *>> & order)
{
  std::set<Node *> executed;
  while (!g.nodes().empty())
  {
    order.push_back({});
    for (auto n : g.roots())
    {
      if (executed.count(n) > 0 && n->isCached())
        continue;

      executed.insert(n);
      order.back().push_back(n);
      g.remove(n);
    }
  }
}

// walk n's dependencies recursively traversing over elemental nodes and stopping at non-elemental
// nodes adding all visited elemental nodes.  The blocking non-elemental nodes are not added to
// the set.  This transitively adds all uncached dependencies of n to the current
// loop/subgraph.
void
floodUp(Node * n, Subgraph & g, LoopType t, int curr_loop)
{
  if ((n->isCached() && n->loop() >= curr_loop))
    return;

  g.add(n);
  for (auto dep : n->deps())
    floodUp(dep, g, t, curr_loop);
}

std::vector<std::vector<std::vector<Node *>>>
computeLoops(Graph & g)
{
  std::vector<std::vector<std::vector<Node *>>> loops;

  int maxloop = 0;
  for (auto n : g.roots())
    if (n->loop() > maxloop)
      maxloop = n->loop();

  std::vector<Subgraph> loopgraphs(maxloop + 1);
  for (auto n : g.nodes())
    loopgraphs[n->loop()].add(n);

  int loopindex = 0;
  for (auto & g : loopgraphs)
  {
    // divide up the loop numbers into each loop subtype
    Subgraph g_node;
    Subgraph g_elem;
    Subgraph g_none;
    Subgraph g_face;
    for (auto n : g.nodes())
    {
      if (n->loopType() == LoopType::Elemental)
        g_elem.add(n);
      else if (n->loopType() == LoopType::Nodal)
        g_node.add(n);
      else if (n->loopType() == LoopType::Face)
        g_face.add(n);
      else if (n->loopType() == LoopType::None)
        g_none.add(n);
    }

    // add in uncached dependencies transitively for each loop type
    for (auto n : g_elem.leaves())
      floodUp(n, g_elem, LoopType::Elemental, n->loop());
    for (auto n : g_node.leaves())
      floodUp(n, g_node, LoopType::Nodal, n->loop());
    for (auto n : g_face.leaves())
      floodUp(n, g_face, LoopType::Face, n->loop());
    for (auto n : g_none.leaves())
      floodUp(n, g_none, LoopType::None, n->loop());

    // topological sort the nodes for each loop
    if (g_elem.nodes().size() > 0)
    {
      loops.push_back({});
      execOrder(g_elem, loops.back());
    }
    if (g_node.nodes().size() > 0)
    {
      loops.push_back({});
      execOrder(g_node, loops.back());
    }
    if (g_face.nodes().size() > 0)
    {
      loops.push_back({});
      execOrder(g_face, loops.back());
    }
    if (g_none.nodes().size() > 0)
    {
      loops.push_back({});
      execOrder(g_none, loops.back());
    }
  }
  std::reverse(loops.begin(), loops.end());
  return loops;
}

void
printLoops(std::vector<std::vector<std::vector<Node *>>> loops)
{
  for (size_t i = 0; i < loops.size(); i++)
  {
    auto & loop = loops[i];
    std::cout << "loop " << i + 1 << ":\n";
    for (size_t g = 0; g < loop.size(); g++)
    {
      auto & group = loop[g];
      std::cout << "    group " << g + 1 << ": ";
      for (auto n : group)
        std::cout << n->str() << ", ";
      std::cout << "\n";
    }
  }
}

void
case1b()
{
  std::cout << "::::: CASE 1b  :::::\n";
  Graph graph;
  auto a = graph.create("a", false, false);
  auto b = graph.create("b", true, true);
  auto c = graph.create("c", false, false);
  auto d = graph.create("d", false, false);

  auto e = graph.create("e", true, true, LoopType::Nodal);
  auto f = graph.create("f", false, false, LoopType::Nodal);
  a->needs(b, c, d);
  b->needs(c);
  e->needs(b);
  f->needs(e);

  auto loops = computeLoops(graph);
  printLoops(loops);
}

void
case1()
{
  std::cout << "::::: CASE 1 :::::\n";
  Graph graph;
  auto a = graph.create("a", false, false);
  auto b = graph.create("b", true, true);
  auto c = graph.create("c", false, false);
  auto d = graph.create("d", false, false);
  a->needs(b, c, d);
  b->needs(c);

  auto loops = computeLoops(graph);
  printLoops(loops);
}

void
case2()
{
  std::cout << "::::: CASE 2 :::::\n";
  Graph graph;
  auto a = graph.create("a", false, false);
  auto b = graph.create("b", true, true);
  auto c = graph.create("c", false, false);
  auto d = graph.create("d", false, false);
  auto e = graph.create("e", true, true);
  auto f = graph.create("f", true, true);
  auto g = graph.create("g", true, true);
  auto h = graph.create("h", false, false);
  auto k = graph.create("k", false, false);
  k->needs(f, g);
  f->needs(b);
  b->needs(a);
  g->needs(a);
  h->needs(e, d);
  e->needs(d);
  d->needs(c, b);

  auto loops = computeLoops(graph);
  printLoops(loops);
}

void
case3()
{
  std::cout << "::::: CASE 3 :::::\n";
  Graph graph;
  auto a = graph.create("a", false, false);
  auto b = graph.create("b", true, true);
  auto c = graph.create("c", false, false);
  auto d = graph.create("d", true, false);
  auto e = graph.create("e", true, true);
  auto f = graph.create("f", true, true);
  auto g = graph.create("g", true, true);
  auto h = graph.create("h", false, false);
  auto k = graph.create("k", false, false);
  k->needs(f, g);
  f->needs(b);
  b->needs(a);
  g->needs(a);
  h->needs(e, d);
  e->needs(d);
  d->needs(c, b);

  auto loops = computeLoops(graph);
  printLoops(loops);
}

int
main(int narg, char ** argv)
{
  case1();
  case1b();
  case2();
  case3();

  return 0;
}
