
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>

// Notes/thoughts:
//
// FV flux kernels depend on pseudo elemental (elem and neighbor) values.  How
// does this fit in to this paradigm?
// DG kernels are evaluated inside the element loop currently.  How does this
// fit in to this paradigm?
// Maybe I'm over-thinking this.  Things just are assigned the loop that they
// are evaluated in in the (moose) code.
//
// Aux variables depend on Aux kernels - this is sort-of backwards from When
// objects depend on only regular (nonlinear) variables, they are actually
// depending only on "cached" prev-timestep values of them - so there is no
// actuall "current" dependency.  These objects are "roots" in the dependency
// tree.  Objects that have nothing else depending on them (i.e. generally
// kernels and outputs) are the leaves of the dependency tree.
//
// Objects are not allowed to depend on objects with a different loop type
// unless the depended on object is cached and has been computed in a
// prior/earlier loop.
//
// How do we recognize "ether" dependencies from regular dependencies - e.g.
// we need to track if a dependency is only on a stateful/old[er] material
// property, or depending on a solution variable.
//
// If a material property is only ever depended on as an old[er] value, then
// we still need to evaluate that property even though it has effectively zero
// dependencies.  Maybe we need to insert a "fake" dependency on this material
// property that just enforces it is calculated every step so it is available
// as old[er] when needed.

// Example scenario to think through:
//
//      KernelA -> MaterialA -> AuxVar -> AuxKernel -> VarA
//                     |                     |
//                     +--> MaterialB -------+-----> VarB
//
// If kernelA and AuxVar are both elemental, then everything can be in the
// same loop.  This is possible because AuxVar+AuxKernel combos are not
// reducing/aggregation values even though they are cached.  If KernelA and
// AuxVar are different loop types, then the must be in separate loops - this
// is allowed/works because AuxVar is cached.
//
// Notable is that material objects sort of "morph" into whatever loop type
// their dependers are.    In a face/fv loop, regular "elemental" material
// property objects work fine - we just initialize two copies of them (elem
// and neighbor) when we run them in the fv/face loop.  When running them in
// elemental loops, we just evaluate them once on volumetric qps like normal.
// Maybe someday we will have nodal materials too.  So maybe the actual
// solution to this is that nodes can have "multiple" loop types - i.e. they
// are allowed to run in one or more of the available loop types. Other
// inter-loop-type dependencies normally require the depended on value to be
// cached, but since materials are "duplicated" into every loop they are in,
// this isn't a problem.

enum class LoopType
{
  None, // represents values calculated "outside" of any loop (e.g. postprocessors that depend on only other postprocessors).
  Nodal,
  Face,
  Elemental,
};

// Nodes in the dependency graph have three properties that we track:
//
//     * stored vs not-stored: their computed value at mesh points is cached and does not need to be
//       recomputed across consecutive loops
//     * loop type: nodal, elemental, etc.
//     * reduction operation vs not: e.g. postprocessors perform a reducing operation.  i.e. when is
//       a value available - immediately upon visiting the location, or not until after an entire
//       loop/reduce operation.
//
// All reducing nodes implicitly cache/store their values, but some non-reducing do as well (e.g. aux variables).  The basic algorithm is as follows:
//
//     1.  If any node depends on a reducing node, it must be calculated in a
//     separate/later loop. - otherwise it can go in the same loop
//
//     2.  Nodes assigned to the same loop in rule 1 that have different loop
//     types must be further split into separate loops.
//
//     3.  Fix remaining dependencies on uncached nodes in another loop by
//     duplicating these uncached nodes into every loop that needs them.

class Node
{
public:
  Node(const std::string & name, bool cached, bool reducing, LoopType l = LoopType::Elemental)
    : _name(name), _cached(cached), _reducing(reducing), _looptype(l)
  {
  }

  // loop returns a loop number for this node.  Loop numbers are ascending as
  // nodes get deeper in the dependency heirarchy. Loop number for a node is
  // equal to the maximum loop number of all nodes that depend on this node,
  // unless this node is reducing (i.e. aggregation) - then the loop number is
  // one greater than the maximum loop number of all nodes that depend on this
  // node.
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

// walk n's dependencies recursively traversing over elemental nodes and
// stopping at nodes of a different loop type, adding all visited elemental
// nodes.  The blocking different-loop-type nodes are not added to the set.
// This also stops on cached dependencies that don't need to be recalculated
// as part of the current loop. This transitively adds all uncached
// dependencies of n to the current loop/subgraph.
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
  // start at all the leaf nodes - i.e. nodes that have "no" dependencies -
  // i.e. things that are either output data or residuals.

  // start at all the root nodes - i.e. things that came from a previous time
  // step or that come from the ether - i.e. solution/variable-values, cached
  // values, etc.  Find the max loop number (most deep in dep tree)
  for (auto n : g.roots())
    if (n->loop() > maxloop)
      maxloop = n->loop();

  // This adds all nodes of a given loop number to a particular loop subgraph.
  std::vector<Subgraph> loopgraphs(maxloop + 1);
  for (auto n : g.nodes())
    loopgraphs[n->loop()].add(n);

  int loopindex = 0;
  for (auto & g : loopgraphs)
  {
    // further divide up each loop subgraph into one subgraph for each loop type.
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

    // add/pull in uncached dependencies transitively for each loop type.
    // This is necessary, because initially each node is assigned only a
    // single loop-number/subgraph.  So we need to duplicate (uncached) nodes
    // that are depended on by nodes in different loops into each of those
    // loops (i.e. material properties).  Cached dependencies do not need to
    // be duplicated since nodes are initially assigned to the highest loop
    // number (ie. deepest/earliest loop) they are needed in.

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
