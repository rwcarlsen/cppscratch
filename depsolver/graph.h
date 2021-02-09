#pragma once

#include <set>
#include <vector>
#include <map>
#include <string>
#include <cassert>
#include <iostream>
#include <memory>
#include <algorithm>
#include <limits>

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
//      KernelA -> MaterialA -> AuxVar -> AuxSolution ->AuxKernel -> VarA
//                     |                                  |
//                     +--> MaterialB -------+------------+--> VarB
//
// If kernelA and AuxVar are both elemental, then everything can be in the
// same loop.  This is possible because AuxVar+AuxSolution+AuxKernel combos are not
// reducing/aggregation values even though they are cached.  If KernelA and
// AuxVar are different loop types, then the must be in separate loops - this
// is allowed/works because AuxSolution is cached.
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
  LoopType(int blk) : block(blk), category(LoopCategory::Elemental_onElem) {}
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
//

class Graph;

class Node
{
public:
  Node(Graph * g, const std::string & name, bool cached, bool reducing, LoopType l)
    : _owner(g), _name(name), _cached(cached), _reducing(reducing), _looptype(l), _visited(false)
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

  // Returns if this node depends on n (directly or transitively).  reachable must
  // contain a superset of reachable nodes from this node - this is used for
  // optimization purposes
  bool isDepender(Node * n)
  {
    unvisitAll();
    return isDependerInner(n);
  }
  void transitiveDependers(std::set<Node *> & all) const
  {
    for (auto d : _dependers)
    {
      all.insert(d);
      d->transitiveDependers(all);
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
    assert(n != this); // cyclical dep check - node can't depend on itself.
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
  bool isDependerInner(Node * n)
  {
    if (_visited)
      return false;
    _visited = true;

    if (_dependers.count(n) > 0)
      return true;
    for (auto d : _dependers)
      if (d->isDependerInner(n))
        return true;
    return false;
  }

  void unvisitAll();

  Graph * _owner;
  std::string _name;
  int _id = -1;
  bool _cached;
  bool _reducing;
  LoopType _looptype;
  std::set<Node *> _deps;
  std::set<Node *> _dependers;
  bool _visited;
};

class Subgraph
{
public:
  Subgraph() {}
  Subgraph(const std::set<Node *> & nodes) : _nodes(nodes) {}

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

  // returns all nodes that depend on n transitively that are within this
  // subgraph.
  void transitiveDependers(Node * n, std::set<Node *> & all) const
  {
    for (auto d : filter(n->dependers()))
    {
      all.insert(d);
      transitiveDependers(d, all);
    }
  }

  // returns all nodes that n depends on transitively that are within this
  // subgraph.
  void transitiveDeps(Node * n, std::set<Node *> & all) const
  {
    for (auto d : filter(n->deps()))
    {
      all.insert(d);
      transitiveDeps(d, all);
    }
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
  virtual void roots(Node * n, std::set<Node *> rts) const
  {
    if (filter(n->deps()).empty())
      rts.insert(n);
    for (auto d : filter(n->deps()))
      roots(d, rts);
  }
  virtual void leaves(Node * n, std::set<Node *> lvs) const
  {
    if (filter(n->dependers()).empty())
      lvs.insert(n);
    for (auto d : filter(n->dependers()))
      leaves(d, lvs);
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

  int _id;
  std::set<Node *> _nodes;
};

class Graph : public Subgraph
{
public:
  template <typename... Args>
  Node * create(Args... args)
  {
    _node_storage.emplace_back(new Node(this, std::forward<Args>(args)...));
    _node_storage.back()->setId(_node_storage.size() - 1);
    add(_node_storage.back().get());
    return _node_storage.back().get();
  }

  const std::vector<std::unique_ptr<Node>> & storage() { return _node_storage; }

private:
  std::vector<std::unique_ptr<Node>> _node_storage;
};

void
Node::unvisitAll()
{
  auto & store = _owner->storage();
  for (int i = 0; i < store.size(); i++)
    store[i]->_visited = false;
}

// this effectively implements a topological sort for the nodes in the graph g. returning a list
// that can be executed that contains groups of nodes that can be run simultaneously.
// Note that g is a by-value arg - because we want a copy to modify during
// this function.
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
    if (order.back().empty())
      order.pop_back();
  }
}

// return a subgraph of g containing all nodes connected to n;
void
findConnected(const Subgraph & g, Node * n, Subgraph & all)
{
  if (all.contains(n) || !g.contains(n))
    return;

  all.add(n);
  for (auto d : n->deps())
    findConnected(g, d, all);
  for (auto d : n->dependers())
    findConnected(g, d, all);
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


// returns true if loops/partitions represented by nodes a and b can be merged.
bool canMerge(Node * a, Node * b)
{
  // this allows us to consider all Elemental_foo loop types mergeable
  std::map<LoopCategory, std::set<LoopCategory>> mergeable = {
    {LoopCategory::None, {LoopCategory::None}},
    {LoopCategory::Nodal, {LoopCategory::Nodal}},
    {LoopCategory::Face, {LoopCategory::Face}},
    {LoopCategory::Elemental_onElem, {LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide}},
    {LoopCategory::Elemental_onElemFV, {LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide}},
    {LoopCategory::Elemental_onBoundary, {LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide}},
    {LoopCategory::Elemental_onInternalSide, {LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide}},
  };

  if (a == b)
    return false;
  if (mergeable[a->loopType().category].count(b->loopType().category) == 0)
    return false;
  if (a->isDepender(b) || b->isDepender(a))
    return false;
  return true;
}

// once we have the graph split into partitions, there are some
// optimizations that can be performed to combine partitions/loops together
// that don't depend on each other.  This algorithm looks at each potential
// merging of two loops/partitions and then calculates which other of the
// candidate mergers are incompatible with it.  The potential merges are then
// sorted by the number of other merges they cancel.  Then we carefully choose
// merges to perform one at a time starting with the ones that cancel the
// fewest other merges - removing the cancelled/incompatible ones
// from the available merge set as we go - until there are not candidate
// merges lest that haven't been already selected or cancelled.
//
// I think if the passed in partitions are ordered appropriately to account
// dor dependencies then the modified/merged partitions should also be in an
// executable order.
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
    assert(loop_node != nullptr);
    loopnode_to_partition[loop_node] = i;
    for (auto n : part.nodes())
      node_to_loopnode[n] = loop_node;
  }

  // construct all inter-partition dependencies
  for (auto & partition : partitions)
    for (auto node : partition.nodes())
      for (auto dep : node->deps())
      {
        if (node_to_loopnode[dep] == node_to_loopnode[node])
          continue;
        assert(dep != nullptr);
        assert(node != nullptr);
        assert(node_to_loopnode.count(node) > 0);
        assert(node_to_loopnode.count(dep) > 0);
        assert(node_to_loopnode[node] != nullptr);
        assert(node_to_loopnode[dep] != nullptr);
        node_to_loopnode[node]->needs(node_to_loopnode[dep]);
      }

  // determine the set of potential merges.
  std::vector<std::pair<Node *, Node *>> candidate_merges;
  std::map<Node *, std::map<Node *, bool>> merge_index;
  for (auto loop1 : graphgraph.nodes())
  {
    for (auto loop2 : graphgraph.nodes())
    {
      // skip if an equivalent merge candidate has already been created
      if (merge_index[loop1][loop2])
        continue;
      if (merge_index[loop2][loop1])
        continue;

      // skip if the two loops are not compatible for merging.
      if (!canMerge(loop1, loop2))
        continue;

      merge_index[loop1][loop2] = true;
      merge_index[loop2][loop1] = true;
      candidate_merges.emplace_back(loop1, loop2);
    }
  }

  // determine which other merges each given merge prevents/cancells from being
  // able to occur.
  std::vector<std::vector<int>> cancellations(candidate_merges.size());
  for (int i = 0; i < candidate_merges.size(); i++)
  {
    auto & candidate = candidate_merges[i];
    auto loop1 = candidate.first;
    auto loop2 = candidate.second;

    for (int j = i + 1; j < candidate_merges.size(); j++)
    {
      auto other1 = candidate_merges[j].first;
      auto other2 = candidate_merges[j].second;

      // swap other1 and other2 nodes so they match up with loop1/loop2 nodes
      if (loop1 == other2 || loop1->isDepender(other2) || other2->isDepender(loop1))
      {
        auto tmp = other1;
        other1 = other2;
        other2 = tmp;
      }

      if (loop1->isDepender(other1) && other2->isDepender(loop2))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (other1->isDepender(loop1) && loop2->isDepender(other2))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (loop1 == other1 && (loop2->isDepender(other2) || other2->isDepender(loop2)))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (loop2 == other2 && (loop1->isDepender(other1) || other1->isDepender(loop1)))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
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
  // remap canceled merge indices using the new sorted indices:
  for (int i = 0; i < sorted_cancellations.size(); i++)
    for (int j = 0; j < sorted_cancellations[i].size(); j++)
      for (int k = 0; k < indices.size(); k++)
        if (sorted_cancellations[i][j] == indices[k])
        {
          sorted_cancellations[i][j] = k;
          break;
        }

  // choose which merges to perform
  std::set<int> canceled_merges;
  std::set<int> chosen_merges;
  for (int i = 0; i < sorted_merges.size(); i++)
  {
    auto & merge = sorted_merges[i];
    if (canceled_merges.count(i) > 0)
      continue;

    chosen_merges.insert(i);
    for (auto cancel : sorted_cancellations[i])
      canceled_merges.insert(cancel);
  }

  // map the merges back into an updated set of new partitions
  // with all the (non-meta) actual objects as nodes.  To do this, we create a
  // parallel set of pointers to the original partitions that we modify/use to
  // make sure consecutive merges of partitions pairs accumulate correctly
  // into single subgraphs - i.e. merging partition 1 and 2 and then later
  // merging partitions 2 and 3 results in a single subgraph representing all
  // nodes for partitions 1, 2, and 3.
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

    // check if previous mergers already caused these two original partitions to become merged;
    // only merge if this wasn't the case.
    if (merged_partitions[part1_index] != merged_partitions[part2_index])
      merged_partitions[part1_index]->merge(*merged_partitions[part2_index]);

    // when two partitions are merged, we need to set the subgraph pointer in both
    // original partitions point to the same subghraph.  Then further merges that
    // may be with already merged partitions can also be placed into the same
    // already-merged subrgraph.  As merges accumulate, we need to keep all
    // these original-partition subgraph entries pointing to the correct
    // single, merged subgraph.
    for (int i = 0; i < merged_partitions.size(); i++)
    {
      // check if prior merges resulted in the current two partitions already being merged.
      // In this case. we don't want to clear out any subgraphs - if we did
      // then we could end up the nodes in the merged partitions being deleted!
      if (merged_partitions[part1_index] == merged_partitions[part2_index])
        break;
      if (i == part1_index)
        continue;
      if (merged_partitions[i] == merged_partitions[part2_index])
      {
        // clear out the partition we merged "from" so that it has zero nodes
        // and we know to remove it from our main partition list/vector once
        // we are done merging.
        merged_partitions[i]->clear();
        merged_partitions[i] = merged_partitions[part1_index];
      }
    }
  }

  // remove the empty partitions that we merged away into other partitions.
  for (auto it = partitions.begin(); it != partitions.end();)
    if (it->nodes().size() == 0)
      it = partitions.erase(it);
    else
      ++it;
}

// takes subgraphs passed in and splits them each into all unconnected
// subgraphs.
std::vector<Subgraph>
splitPartitions(std::vector<Subgraph> & partitions)
{
  std::vector<Subgraph> splits;

  for (auto & g : partitions)
  {
    std::set<Node *> roots = g.roots();
    while (roots.size() > 0)
    {
      Node * r = *roots.begin();
      Subgraph split;
      findConnected(g, r, split);
      for (auto r : split.roots())
        roots.erase(r);
      splits.push_back(split);
    }
  }

  // This was an alternative approach where we just dup/split out every root and its
  // deps as a separate subgraph.  I don't think this works well for
  // larger-scale optimization - we end up splitting graphs into pieces that
  // otherwise share dependencies and so this would cause redundant/duplicate
  // calculations to be performed
  //for (auto & g : partitions)
  //{
  //  for (auto lv : g.leaves())
  //  {
  //    Subgraph split;
  //    std::set<Node *> tdeps = {lv};
  //    g.transitiveDeps(lv, tdeps);
  //    for (auto n : tdeps)
  //      split.add(n);
  //    splits.push_back(split);
  //  }
  //}

  return splits;
}

std::vector<Subgraph>
computePartitions(Graph & g, bool merge = false)
{
  std::vector<Subgraph> partitions;

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


    for (auto & entry : subgraphs)
      partitions.push_back(entry.second);
  }

  // add/pull in uncached dependencies transitively for each loop type.
  // This is necessary, because initially each node is assigned only a
  // single loop-number/subgraph.  So we need to duplicate (uncached) nodes
  // that are depended on by nodes in different loops into each of those
  // loops (i.e. material properties).  Cached dependencies do not need to
  // be duplicated since nodes are initially assigned to the highest loop
  // number (ie. deepest/earliest loop) they are needed in.
  for (auto & g : partitions)
    for (auto n : g.leaves())
      floodUp(n, g, n->loopType(), n->loop());

  // TODO: decide which way:
  //
  // We need further split each of these loops/partitions into
  // unconnected subgraphs - this will facilitate better merging/optimization
  // later.  This splitting needs to occur *before* we floodUp -
  // otherwise some of those pulled-in (uncached) dependencies may make
  // previously unconnected portions of the graph look connected - when in
  // reality, we want them to be split (and hence look unconnected).
  //
  // Or maybe it is better to floodup first - because unconnected subgraphs
  // that become connected - that means they share nodes - which can reduce
  // calculations if they stay unsplit/together.
  partitions = splitPartitions(partitions);

  // make sure there aren't any dependency nodes that aren't included in at
  // least one partition
  assert([&](){
        std::set<Node *> all_deps;
        std::set<Node *> all_nodes;
        for (auto & g : partitions)
        {
          for (auto n : g.nodes())
          {
            all_nodes.insert(n);
            for (auto d : n->deps())
              all_deps.insert(d);
          }
        }

        for (auto d : all_deps)
          if (all_nodes.count(d) == 0)
            return false;
        return true;
      }());

  if (merge)
    mergeSiblings(partitions);
  return partitions;
}

std::vector<std::vector<std::vector<Node *>>>
computeLoops(std::vector<Subgraph> & partitions)
{
  std::vector<std::vector<std::vector<Node *>>> loops;
  // topological sort the nodes for each loop
  for (auto & g : partitions)
  {
    loops.push_back({});
    execOrder(g, loops.back());
  }
  std::reverse(loops.begin(), loops.end());
  return loops;
}

