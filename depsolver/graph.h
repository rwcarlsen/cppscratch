#pragma once

#include <set>
#include <vector>
#include <map>
#include <string>
#include <iostream>

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
  LoopType(LoopCategory cat = LoopCategory::Elemental_onElem, int blk = 0) : block(blk), category(cat) {}
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
  int block;
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
    {
      // TODO: does the loop number really need to increment if the loop type
      // changes - is this the right logic?
      if (dep->loopType() != loopType() || isReducing())
      {
        if (dep->loop() + 1 > maxloop)
          maxloop = dep->loop() + 1;
      }
      else
      {
        if (dep->loop() > maxloop)
          maxloop = dep->loop();
      }
    }
    return maxloop;
  }

  std::set<Node *> deps() const { return _deps; }
  std::set<Node *> dependers() const { return _dependers; }
  bool isDepender(Node * n) const
  {
    std::set<Node *> all;
    transitiveDependers(all);
    return all.count(n) > 0;
  }
  void transitiveDependers(std::set<Node *> & all) const
  {
    for (auto n : _dependers)
    {
      all.insert(n);
      n->transitiveDependers(all);
    }
  }

  bool isReducing() const { return _reducing; }
  bool isCached() const { return _cached || _reducing; }
  LoopType loopType() const { return _looptype; }

  std::string str() { return _name; }
  std::string name() { return _name; }

  void needs() {}

  void clearDeps() {_deps.clear(); _dependers.clear();}

  template <typename... Args>
  void needs(Node * n, Args... args)
  {
    _deps.insert(n);
    n->_dependers.insert(this);
    needs(args...);
  }
  void needs(const std::set<Node *> & deps)
  {
    for (auto dep : deps)
      needs(dep);
  }

  int id() {return _id;}

  void setId(int id)
  {
    if (_id != -1)
      throw std::runtime_error("setting node id multiple times");
    if (id == -1)
      throw std::runtime_error("cannot set node id to -1");
    _id = id;
  }

private:
  std::string _name;
  int _id = -1;
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
  Subgraph(const std::set<Node *> & nodes) : _nodes(nodes) {_id = _next_id++;}

  // Returns the minimum number of jumps it takes to get from any root node of
  // the dependency graph to this node.
  int mindepth(Node * n)
  {
    auto deps = filter(n->deps());
    if (deps.size() == 0)
      return 0;

    int min = std::numeric_limits<int>::max();
    for (auto dep : deps)
    {
      if (mindepth(dep) + 1 < min)
        min = mindepth(dep) + 1;
    }
    return min;
  }
  // returns true if any nodes in this graph are reachable from or dependend on
  // transitively by the given from nodes.
  bool reachable(std::set<Node *> from)
  {
    for (auto n : from)
    {
      if (contains(n))
        return true;
      if (reachable(n->deps()))
        return true;
    }
    return false;
  }
  // returns a subgraph of all nodes that are reachable (depended on
  // transitively) by the given from node.
  Subgraph reachableFrom(Node * from)
  {
    Subgraph can_reach;
    for (auto n : nodes())
    {
      Subgraph g({n});
      if (g.reachable({from}))
        can_reach.add(n);
    }
    return can_reach;
  }
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
  virtual void clear() { _nodes.clear(); }
  virtual void merge(const Subgraph & other)
  {
    if (&other != this)
      _nodes.insert(other._nodes.begin(), other._nodes.end());
  }
  int id() const {return _id;}
  int size() const {return _nodes.size();}

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
    _node_storage.back()->setId(_node_storage.size() - 1);
    add(_node_storage.back().get());
    return _node_storage.back().get();
  }

  Graph clone()
  {
    Graph copy;
    // make a copy of all the nodes
    for (auto & n : _node_storage)
      copy._node_storage.emplace_back(new Node(*n));

    for (int i = 0; i < copy._node_storage.size(); i++)
    {
      auto n_orig = _node_storage[i].get();
      auto n_copy = copy._node_storage[i].get();
      // reset each node's dependencies, reconnecting each node to the new copies
      n_copy->clearDeps();
      for (auto dep : n_orig->deps())
        n_copy->needs(copy._node_storage[dep->id()].get());
    }
    return copy;
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

// TODO: once we have the graph split into partitions, there are some
// optimizations that can be performed to combine partitions/loops together
// that don't depend on each other.  The algorithm will need to:
//
// * look at every combination of loop/partition pairs
//
// * determine if the two partitions are siblings - this means that neither is
//   a dependency of the other and neither depends on the other.
//
// * if they are siblings and have the same loop type, then they can be
//   merged
void
mergeSiblings(std::vector<Subgraph> & partitions)
{
  // create a graph where each node represents one of the total dep graph partitions
  std::map<Node *, Node *> node_to_loopnode;
  std::map<Node *, int> loopnode_to_partition;
  Graph graphgraph;
  for (int i = 0; i < partitions.size(); i++)
  {
    auto & part = partitions[i];
    auto loop_node = graphgraph.create("loop" + std::to_string(i), false, false, (*part.nodes().begin())->loopType());
    loopnode_to_partition[loop_node] = i;
    for (auto n : part.nodes())
      node_to_loopnode[n] = loop_node;
  }

  // construct all inter-partition dependencies
  for (int i = 0; i < graphgraph.nodes().size(); i++)
  {
    for (auto loopnode : graphgraph.nodes())
    {
      for (auto n : partitions[loopnode_to_partition[loopnode]].nodes())
      {
        for (auto dep : n->deps())
          if (node_to_loopnode[dep] != loopnode)
            loopnode->needs(node_to_loopnode[dep]);
      }
    }
  }

  // determine the set of potential merges.
  std::vector<std::pair<Node *, Node *>> candidate_merges;
  std::map<Node *, std::map<Node *, int>> merge_index;
  for (auto loop1 : graphgraph.nodes())
  {
    for (auto loop2 : graphgraph.nodes())
    {
      if (loop1 == loop2)
        continue;
      if (loop1->isDepender(loop2) || loop2->isDepender(loop1))
        continue;
      if (loop1->loopType().category != loop2->loopType().category)
        continue;

      if (merge_index.count(loop1) > 0)
        if (merge_index[loop1].count(loop2) > 0)
          continue;

      merge_index[loop1][loop2] = candidate_merges.size();
      merge_index[loop2][loop1] = candidate_merges.size();
      candidate_merges.emplace_back(loop1, loop2);
    }
  }

  // determine how many merges each given merge prevents/cancells from being
  // able to occur.
  std::vector<std::vector<int>> cancellations(candidate_merges.size());
  for (int i = 0; i < candidate_merges.size(); i++)
  {
    auto & candidate = candidate_merges[i];
    auto loop1 = candidate.first;
    auto loop2 = candidate.second;

    for (int j = 0; j < candidate_merges.size(); j++)
    {
      if (i == j)
        continue;

      auto other1 = candidate_merges[j].first;
      auto other2 = candidate_merges[j].second;

      // swap other1 and other 2 nodes so they match up with loop1/loop2 nodes
      if (loop1->isDepender(other2) || other2->isDepender(loop1) || loop1 == other2)
      {
        auto tmp = other1;
        other1 = other2;
        other2 = tmp;
      }

      if (loop1->isDepender(other1) && other2->isDepender(loop2))
        cancellations[i].push_back(j);
      else if (other1->isDepender(loop1) && loop2->isDepender(other2))
        cancellations[i].push_back(j);
      else if (loop1 == other1 && (loop2->isDepender(other2) || other2->isDepender(loop2)))
        cancellations[i].push_back(j);
      else if (loop2 == other2 && (loop1->isDepender(other1) || other1->isDepender(loop1)))
        cancellations[i].push_back(j);
    }
  }

  // sort the merges by fewest to most cancellations;
  std::vector<int> indices(candidate_merges.size());
  for (int i = 0; i < indices.size(); i++)
    indices[i] = i;
  std::sort(indices.begin(), indices.end(), [&](int a, int b) {return cancellations[a].size() < cancellations[b].size();});

  std::vector<std::pair<Node *, Node *>> sorted_merges(indices.size());
  std::vector<std::vector<int>> sorted_cancellations(indices.size());
  for (int i = 0; i < indices.size(); i++)
  {
    int index = indices[i];
    sorted_merges[i] = candidate_merges[index];
    sorted_cancellations[i] = cancellations[index];
  }

  // choose which merges to perform
  std::set<int> canceled_merges;
  std::set<int> chosen_merges;
  for (int i = 0; i < sorted_merges.size(); i++)
  {
    if (canceled_merges.count(i) > 0)
      continue;

    auto & merge = sorted_merges[i];
    auto num_cancel = sorted_cancellations[i].size();
    chosen_merges.insert(i);
    for (auto cancel : sorted_cancellations[i])
      canceled_merges.insert(cancel);
  }

  // TODO: map the merged graph back into an updated set of new partitions
  // with all the (non-meta) actual objects as nodes.
  std::vector<Subgraph *> merged_partitions(partitions.size());
  for (int i = 0; i < partitions.size(); i++)
    merged_partitions[i] = &partitions[i];
  for (auto merge_index : chosen_merges)
  {
    auto & merge = sorted_merges[merge_index];
    auto loop1 = merge.first;
    auto loop2 = merge.second;
    auto part1_index = loopnode_to_partition[loop1];
    auto part2_index = loopnode_to_partition[loop2];

    // check if a previous mergers already caused these two original partitions to become merged
    if (merged_partitions[part1_index] == merged_partitions[part2_index])
      continue;

    merged_partitions[part1_index]->merge(*merged_partitions[part2_index]);
    merged_partitions[part2_index]->clear();
    merged_partitions[part2_index] = merged_partitions[part1_index];
  }

  for (auto it = partitions.begin(); it != partitions.end(); ++it)
    if (it->nodes().size() == 0)
      partitions.erase(it);
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

