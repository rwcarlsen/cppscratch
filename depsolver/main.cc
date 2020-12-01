
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>
#include <sstream>

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

enum class LoopCategory
{
  None, // represents values calculated "outside" of any loop (e.g. postprocessors that depend on only other postprocessors).
  Nodal,
  Face, // FV
  Elemental_onElem,
  Elemental_onElemFV, // different quadrature points than normal/FE
  Elemental_onBoundary,
  Elemental_onInternalSide,
};

class LoopType
{
public:
  LoopType(LoopCategory cat = LoopCategory::Elemental_onElem, unsigned int blk = 0) : block(blk), category(cat) {}
  bool operator==(const LoopType & other)
  {
    return other.block == block && other.category == category;
  }
  bool operator!=(const LoopType & other)
  {
    return !(*this == other);
  }
  bool operator<(const LoopType & other) const
  {
    return category != other.category ? category < other.category : block < other.block;
  }
  // subdomain/block or boundary ID
  unsigned int block;
  LoopCategory category;
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
  Node(const std::string & name, bool cached, bool reducing, LoopType l)
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
  Subgraph() {_id = _next_id++;}
  Subgraph(std::set<Node *> & nodes) : _nodes(nodes) {_id = _next_id++;}
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
  int id() const {return _id;}

private:
  std::set<Node *> filter(std::set<Node *> ns) const
  {
    std::set<Node *> filtered;
    for (auto n : ns)
      if (contains(n))
        filtered.insert(n);
    return filtered;
  }

  static int _next_id;
  int _id;
  std::set<Node *> _nodes;
};

int Subgraph::_next_id = 1;

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
  if (n->loopType() != t)
    return;
  if (n->isCached() && (n->loop() > curr_loop))
    return;

  g.add(n);
  for (auto dep : n->deps())
    floodUp(dep, g, t, curr_loop);
}

std::vector<std::vector<std::vector<Node *>>>
computeLoops(Graph & g, std::vector<Subgraph> & partitions)
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
    std::map<LoopType, Subgraph> subgraphs;
    for (auto n : g.nodes())
    {
      if (subgraphs.count(n->loopType()) == 0)
        subgraphs[n->loopType()] = {};
      subgraphs[n->loopType()].add(n);
    }

    // add/pull in uncached dependencies transitively for each loop type.
    // This is necessary, because initially each node is assigned only a
    // single loop-number/subgraph.  So we need to duplicate (uncached) nodes
    // that are depended on by nodes in different loops into each of those
    // loops (i.e. material properties).  Cached dependencies do not need to
    // be duplicated since nodes are initially assigned to the highest loop
    // number (ie. deepest/earliest loop) they are needed in.

    for (auto & entry : subgraphs)
    {
      auto & g = entry.second;
      for (auto n : g.leaves())
        floodUp(n, g, n->loopType(), n->loop());
    }

    // topological sort the nodes for each loop
    for (auto & entry : subgraphs)
    {
      auto & g = entry.second;
      partitions.push_back(g);
      loops.push_back({});
      execOrder(g, loops.back());
    }
  }
  std::reverse(loops.begin(), loops.end());
  return loops;
}

std::string
loopTypeStr(const LoopType & l)
{
  std::string s = "Loop:";
  if (l.category == LoopCategory::None)
    s += "None";
  else if (l.category == LoopCategory::Nodal)
    s += "Nodal";
  else if (l.category == LoopCategory::Face)
    s += "Face";
  else if (l.category == LoopCategory::Elemental_onElem)
    s += "Elemental_onElem";
  else if (l.category == LoopCategory::Elemental_onElemFV)
    s += "Elemental_onElemFV";
  else if (l.category == LoopCategory::Elemental_onBoundary)
    s += "Elemental_onBoundary";
  else if (l.category == LoopCategory::Elemental_onInternalSide)
    s += "Elemental_onInternalSide";
  else
    s += "UNKNOWN";
  s += ":block" + std::to_string(l.block);
  return s;
}

std::string
nodeLabel(const Subgraph & g, Node * n)
{
  std::string s = n->str() + " on partition " + std::to_string(g.id()) + "\\n";
  s += loopTypeStr(n->loopType());
  if (n->isCached() || n->isReducing())
  {
    s += "\\n(";
    if (n->isCached())
    {
      s += "cached";
      if (n->isReducing())
        s += ",";
    }
    if (n->isReducing())
      s += "reducing";
    s += ")";
  }
  return s;
}

// outputs an a dot script edge pointing from src to dst.  If dst is nullptr,
// then src is created as an "island" node.  If dst is not contained in the
// subgraph g, then the node is filled with an off-yellow color - indicating
// that this node represents a value that is cached and should already be
// available (i.e. computed in a prior loop) - filled nodes are not
// (re)calculated in the current loop, just (re)used.
std::string
dotEdge(const Subgraph & g, Node * src, Node * dst)
{
  if (dst)
  {
    if (g.contains(dst))
      return "\"" + nodeLabel(g, src) + "\" -> \"" + nodeLabel(g, dst) + "\";\n";
    std::string dstlabel = "\"" + nodeLabel(g, dst) + "\"";
    return "\"" + nodeLabel(g, src) + "\" -> " + dstlabel + ";\n" + dstlabel  + " [style=filled, fillcolor=khaki];\n";
  }
  return "\"" + nodeLabel(g, src) + "\";\n";
}

std::string
dotConnections(const Subgraph & g)
{
  std::stringstream ss;
  for (auto n : g.nodes())
  {
    bool island = true;
    for (auto dep : n->deps())
    {
      island = false;
      ss << dotEdge(g, n, dep);
    }
    for (auto dep : n->dependers())
      if (g.contains(dep))
        island = false;

    if (island)
      ss << dotEdge(g, n, nullptr);
  }
  return ss.str();
}

// show all the given subgraphs on a single graph.
std::string
dotGraphMerged(const std::vector<Subgraph> & graphs)
{
  std::stringstream ss;
  ss << "digraph g {\n";
  for (auto & g : graphs)
    ss << dotConnections(g);
  ss << "}\n";
  return ss.str();
}

std::string
dotGraph(const Subgraph & g)
{
  std::stringstream ss;
  ss << "digraph g {\n";
  dotConnections(g);
  ss << "}\n";
  return ss.str();
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
  //printLoops(loops);
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
  case3();

  return 0;
}
