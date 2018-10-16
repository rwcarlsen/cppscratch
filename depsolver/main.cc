
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
  Node * needs(Node * n)
  {
    _deps.insert(n);
    n->_dependers.insert(this);
    return n;
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
    if (!dep->is_elemental() && dep->loop() > candidate_loop)
      return false;
  return true;
}

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

int
main(int narg, char ** argv)
{
  Graph graph;
  auto a = graph.create(true, "a");
  auto b = graph.create(false, "b");
  auto c = graph.create(true, "c");
  auto d = graph.create(true, "d");
  a->needs(b);
  a->needs(c);
  a->needs(d);
  b->needs(c);

  auto loops = computeLoops(graph);

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

  std::cout << "a.loop = " << a->loop() << "\n";
  std::cout << "b.loop = " << b->loop() << "\n";
  std::cout << "c.loop = " << c->loop() << "\n";

  return 0;
}
