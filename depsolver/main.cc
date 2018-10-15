
#include <iostream>
#include <set>
#include <vector>
#include <map>
#include <list>

int
main(int narg, char ** argv)
{
  return 0;
}

class Node
{
public:
  void setLoop(int loop) { _loop = loop; }
  int loop() { return _loop; }
  std::set<Node *> deps() { throw std::runtime_error("not implemented"); }
  bool is_elemental() { return _elemental; };

private:
  bool _elemental = true;
  int _loop = -1;
};

class Graph
{
public:
  std::set<Node *> leaves() { throw std::runtime_error("not implemented"); }
};

// builds a sorted list of all dependencies - including transitive - of n that can be reached
// without traversing non-elemental nodes and inserts them into results in a resolved order ready
// for execution.
void
sortElementalDeps(Node * n, std::vector<Node *> results)
{
  throw std::runtime_error("not implemented");
}

void
collectLoops(Node * n, std::vector<std::vector<Node *>> loops)
{
  auto & curr_loop = loops[n->loop()];
  curr_loop.push_back(n);
  sortElementalDeps(n, curr_loop);

  for (auto dep : n->deps())
  {
    if (!dep->is_elemental())
      collectLoops(dep, loops);
  }
}

void
collectElemental(Node * n, std::set<Node *> & not_elemental)
{
  for (auto dep : n->deps())
  {
    if (!dep->is_elemental())
      not_elemental.insert(dep);
    else
      collectElemental(dep, not_elemental);
  }
}

std::list<std::vector<Node *>>
buildLoops(Graph & g)
{
  std::set<Node *> queue = g.leaves(); // iniitalize this with leaves of dep graph
  std::set<Node *> next_queue;
  int loop = 0;
  while (!queue.empty())
  {
    for (auto node : queue)
    {
      node->setLoop(loop); // last setLoop call wins/overwrites
      collectElemental(node, next_queue);
    }
    std::swap(queue, next_queue);
    next_queue.clear();
    loop++;
  }

  // then we traverse entire graph collecting all nodes that need to run with each node that has a
  // loop number set - this may end up duplicating elemental nodes (i.e. having them run more than
  // once since they have to be recomputed every loop).
  std::list<std::vector<Node *>> loops(loop);
  for (auto n : g.leaves())
  {
  }
  return loops;
}
