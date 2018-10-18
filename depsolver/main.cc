
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>

class Node
{
public:
  Node(bool elemental, const std::string & name = "") : _name(name), _elemental(elemental) {}
  void setLoop(int loop) { _loop = loop; }
  int loop() const { return _loop; }
  std::set<Node *> deps() const { return _deps; }
  std::set<Node *> dependers() const { return _dependers; }
  bool is_elemental() const { return _elemental; };
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
  bool _elemental = true;
  int _loop = std::numeric_limits<int>::max();
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
  while (!g.nodes().empty())
  {
    order.push_back({});
    for (auto n : g.roots())
    {
      order.back().push_back(n);
      g.remove(n);
    }
  }
}

// Note that in a more complete implementation floodDown and floodUp for what
// we need we will have to also chop/block the graph walking if a node needs a different
// loop type than the type of loop being generated for currently (e.g. nodal vs elemental).

// Also, non-elemental nodes will only need to be recomputed every loop if they don't store/cache
// the values they compute (aut kernels store their values like this).  So in actuality, Ovjects
// will have several properties:
//
//     * stored vs not-stored: their computed value at mesh points is cached and does not need to be
//       recomputed across consecutive loops
//     * loop type: nodal, elemental, etc.
//     * reduction operation vs not: e.g. postprocessors perform a reducing operation.
//
// All reducing nodes store their values, but some non-reducing do as well.
// A more complete implementation will require:
//
// * When flooding down or up, we stop and chop the graph below reducing nodes or when a node's
//   loop type differs from the loop type currently being worked on.
//
// * When generating the loops, we need to proceed from nodes as high in the global
//   graph as possible the node highest in this graph will "choose" which loop type we should
//   generate next.  This will prevent nodes later in the graph from being erroneously
//   assigned a higher loop number than they need to because loops of a certain type haven't been
//   generated yet.
//
// * When flooding up, we actually need to chop the graph below all stored value nodes (not just
//   below reducing nodes) in addition to nodes that have a different loop type.
//
// * non-stored nodes cannot depend on other non-stored objects that have a different loop type -
//   it doesn't make sense. We should have an error check to prevent this.

// walk n's dependencies recursively traversing over elemental nodes and stopping at non-elemental
// nodes adding all visited elemental nodes.  The blocking non-elemental nodes are not added to
// the set.
void
floodUp(Node * n, Subgraph & g)
{
  if (!n->is_elemental())
    return;

  g.add(n);
  for (auto dep : n->deps())
    floodUp(dep, g);
}

// walk nodes that depend on n recursively traversing over elemental nodes and stopping at
// non-elemental nodes adding all visited nodes.  The blocking non-elemental nodes are added to the
// set although they are not traversed over.
void
floodDown(Node * n, Subgraph & g)
{
  g.add(n);
  if (!n->is_elemental())
    return;

  for (auto dep : n->dependers())
    floodDown(dep, g);
}

// returns true if a node n can be executed in the given candidate loop number
bool
canrun(Node * n, int candidate_loop)
{
  // if any dependency of this node is a non-elemental/aggregating node that has not yet been
  // assigned to run in a loop prior to the candidate loop, then the node cannot run in the
  // candidate loop. In a more complete implementation, this will actually need to check if n
  // has any non-executed stored dependencies rather than non-executed reduction/aggregation
  // dependencies.
  for (auto dep : n->deps())
    if (!dep->is_elemental() && dep->loop() > candidate_loop)
      return false;
  return true;
}

// this walks up the graph recursively from n cancelling nodes for which all dependers have been
// cancelled.  Note that this algorithm is imperfect/incomplete - it could miss nodes that ought
// to be cancelled because cancel-propogation hasn't yet worked its way up the tree far enough to
// cancell all a node's dependers.
void
cancelUp(Node * n, Subgraph & cancelled)
{
  // abort recursion if any depender is not cancelled
  if (!cancelled.contains(n))
    for (auto dep : n->dependers())
      if (!cancelled.contains(dep))
        return;

  // all of n's dependers have been cancelled. Cancel n and check if any of its dependencies need
  // to be cancelled.
  cancelled.add(n);
  for (auto dep : n->deps())
    if (dep->is_elemental())
      cancelUp(dep, cancelled);
}

std::vector<std::vector<std::vector<Node *>>>
computeLoops(Graph & g)
{
  std::vector<std::vector<std::vector<Node *>>> loops;

  auto queue = g.roots();
  int loop = 0;
  while (!queue.empty())
  {
    Subgraph g;
    for (auto n : queue)
      floodDown(n, g);

    for (auto n : g.leaves())
      floodUp(n, g);

    Subgraph cancelled;
    for (auto n : g.nodes())
      if (!canrun(n, loop))
        floodDown(n, cancelled);

    // Note that this is not necessary for correctness - it only serves as an optimization to
    // remove superfluous node executions.
    for (auto n : cancelled.leaves())
      cancelUp(n, cancelled);

    for (auto n : cancelled.nodes())
      g.remove(n);

    for (auto n : g.nodes())
      n->setLoop(loop);

    loops.push_back({});
    execOrder(g, loops.back());

    // prepare for computing next loop
    queue.clear();
    for (auto n : g.leaves())
      for (auto dep : n->dependers())
        queue.insert(dep);
    loop++;
  }
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
      std::cout << "    group " << i + 1 << ": ";
      for (auto n : group)
        std::cout << n->str() << ", ";
      std::cout << "\n";
    }
  }
}

void
case1()
{
  std::cout << "::::: CASE 1 :::::\n";
  Graph graph;
  auto a = graph.create(true, "a");
  auto b = graph.create(false, "b");
  auto c = graph.create(true, "c");
  auto d = graph.create(true, "d");
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
  auto a = graph.create(true, "a");
  auto b = graph.create(false, "b");
  auto c = graph.create(true, "c");
  auto d = graph.create(true, "d");
  auto e = graph.create(false, "e");
  auto f = graph.create(false, "f");
  auto g = graph.create(false, "g");
  auto h = graph.create(true, "h");
  auto k = graph.create(true, "k");
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
  case2();

  return 0;
}
