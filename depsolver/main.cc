
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>

class Node
{
public:
  Node(bool elemental) : _elemental(elemental) {}
  void setLoop(int loop) { _loop = loop; }
  int loop() const { return _loop; }
  std::set<Node *> deps() const { return _deps; }
  std::set<Node *> dependers() const { return _dependers; }
  bool is_elemental() const { return _elemental; };
  Node * needs(Node * n)
  {
    _deps.insert(n);
    n->_dependers.insert(this);
    return n;
  }

private:
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
    {
      std::cout << "roots.size=" << rs.size() << "\n";
      if (filter(n->deps()).empty())
        rs.insert(n);
    }
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
  void add(Node * n) { _nodes.insert(n); }
  void remove(Node * n) { _nodes.erase(n); }
  bool contains(Node * n) const { return _nodes.count(n) > 0; }
  std::set<Node *> nodes() const { return _nodes; }

private:
  std::set<Node *> filter(std::set<Node *> ns) const
  {
    for (auto n : ns)
      if (!contains(n))
        ns.erase(n);
    return ns;
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

void
execOrder(Subgraph & g, std::vector<std::vector<Node *>> & order)
{
  while (!g.nodes().empty())
  {
    std::cout << "g.nodes.size=" << g.nodes().size() << "\n";
    std::cout.flush();
    order.push_back({});
    std::cout << "g.roots.size=" << g.roots().size() << "\n";
    std::cout.flush();
    for (auto n : g.roots())
    {
      std::cout << "erasing root\n";
      std::cout.flush();
      order.back().push_back(n);
      g.remove(n);
    }
  }
}

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

bool
canrun(Node * n, int candidate_loop)
{
  for (auto dep : n->deps())
    if (dep->loop() > candidate_loop)
      return false;
  return true;
}

std::vector<std::vector<std::vector<Node *>>>
computeLoops(Graph & g)
{
  std::vector<std::vector<std::vector<Node *>>> loops;

  auto queue = g.roots();
  int loop = 0;
  while (!queue.empty())
  {
    std::cout << "loop=" << loop << "\n";
    Subgraph g;
    std::cout << "    spot1\n";
    for (auto n : queue)
      floodDown(n, g);
    std::cout << "    spot2\n";
    for (auto n : g.leaves())
      floodUp(n, g);
    std::cout << "    spot3\n";

    Subgraph cancelled;
    for (auto n : g.roots())
      if (!canrun(n, loop))
        floodDown(n, cancelled);
    std::cout << "    spot4\n";

    for (auto n : cancelled.nodes())
      g.remove(n);
    std::cout << "    spot5\n";

    for (auto n : g.nodes())
      n->setLoop(loop);
    std::cout << "    spot6\n";

    loops.push_back({});
    execOrder(g, loops.back());
    std::cout << "    spot7\n";

    // prepare for computing next loop
    queue.clear();
    for (auto n : g.leaves())
      for (auto dep : n->dependers())
        queue.insert(dep);
    loop++;
  }
  return loops;
}

int
main(int narg, char ** argv)
{
  Graph graph;
  auto a = graph.create(true);
  auto b = graph.create(false);
  auto c = graph.create(true);
  a->needs(b);
  a->needs(c);
  b->needs(c);

  auto loops = computeLoops(graph);

  std::cout << "a.loop = " << a->loop() << "\n";
  std::cout << "b.loop = " << b->loop() << "\n";
  std::cout << "c.loop = " << c->loop() << "\n";

  return 0;
}
