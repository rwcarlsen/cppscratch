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
  Node(Graph * g, const std::string & name, bool cached, bool reducing, LoopType l);

  std::set<Node *> deps() const;
  std::set<Node *> dependers() const;

  // Returns true if n depends on this node (directly or transitively).  reachable must
  // contain a superset of reachable nodes from this node - this is used for
  // optimization purposes.
  bool dependsOn(Node * n);

  // Stores in all every node that depends on this node transitively.
  void transitiveDependers(std::set<Node *> & all) const;

  // Stores in all every node that this node depends on transitively.
  void transitiveDeps(std::set<Node *> & all) const;

  bool isReducing() const;
  bool isCached() const;
  LoopType loopType() const;

  std::string str();
  std::string name();

  void clearDeps();

  void needs() {}

  template <typename... Args>
  void needs(Node * n, Args... args)
  {
    assert(n != this); // cyclical dep check - node can't depend on itself.
    _deps.insert(n);
    _n_visits++;
    n->inheritDependers(this, _transitive_dependers);
    n->_dependers.insert(this);
    needs(args...);
  }
  void needs(const std::set<Node *> & deps);

  int id();

  void setId(int id);

  // loop returns a loop number for this node.  Loop numbers are ascending as
  // nodes get deeper in the dependency heirarchy. Loop number for a node is
  // equal to the maximum loop number of all nodes that depend on this node,
  // unless this node is reducing (i.e. aggregation) - then the loop number is
  // one greater than the maximum loop number of all nodes that depend on this
  // node.
  //
  // prepare must have been called once for at least one node in the same
  // graph as this node before you access this information.
  int loop();

  // This is used to precalculate all the loop() numbers for each node in the same graph as this node.
  // must be called before accessing loop number information for any node in the same graph as this node.
  void prepare();

private:
  // Allows incrementally building up the transitive dependers/deps lists for every
  // node as new dependencies are added between nodes.
  void inheritDependers(Node * n, std::set<Node *> & dependers);
  int loopInner();

  int _visit_count = 0;
  static int _n_visits;

  int _loop = -1;
  std::set<Node *> _transitive_dependers;
  Graph * _owner;

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
  Subgraph() {}
  Subgraph(const std::set<Node *> & nodes) : _nodes(nodes), _id(_next_id++) {}

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

  // This calls prepare for all nodes in the graph (not just this subgraph).
  void prepare()
  {
    if (_nodes.size() > 0)
      (*_nodes.begin())->prepare();
  }
  // returns true if any nodes in this graph are reachable from or dependend on
  // transitively by the given from nodes.
  bool reachable(std::set<Node *> from)
  {
    std::set<Node *> transitive_deps;
    for (auto n : from)
      n->transitiveDeps(transitive_deps);

    for (auto n : _nodes)
      if (transitive_deps.count(n) > 0)
        return true;
    return false;
  }

  // returns all nodes that depend on n transitively that are within this
  // subgraph.
  void transitiveDependers(Node * n, std::set<Node *> & all) const
  {
    for (auto d : filter(n->dependers()))
    {
      if (all.count(d) > 0)
        continue;
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
      if (all.count(d) > 0)
        continue;
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

  static int _next_id;
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

// this effectively implements a topological sort for the nodes in the graph g. returning a list
// that can be executed that contains groups of nodes that can be run simultaneously.
// Note that g is a by-value arg - because we want a copy to modify during
// this function.
void execOrder(Subgraph g, std::vector<std::vector<Node *>> & order);

// return a subgraph of g containing all nodes connected to n;
void findConnected(const Subgraph & g, Node * n, Subgraph & all);

// walk n's dependencies recursively traversing over elemental nodes and
// stopping at nodes of a different loop type, adding all visited elemental
// nodes.  The blocking different-loop-type nodes are not added to the set.
// This also stops on cached dependencies that don't need to be recalculated
// as part of the current loop. This transitively adds all uncached
// dependencies of n to the current loop/subgraph.
void floodUp(Node * n, Subgraph & g, LoopType t, int curr_loop);

// returns true if loops/partitions represented by nodes a and b can be merged.
bool canMerge(Node * a, Node * b);

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
void mergeSiblings(std::vector<Subgraph> & partitions);

// This merges all pairs of partitions together for which one is a subset of
// the other - i.e. all nodes of one partitions exist in the other partition.
void mergeSubsets(std::vector<Subgraph> & partitions);

// takes subgraphs passed in and splits them each into all unconnected
// subgraphs.
std::vector<Subgraph> splitPartitions(std::vector<Subgraph> & partitions);

std::vector<Subgraph> computePartitions(Graph & g, bool merge = false);

std::vector<std::vector<std::vector<Node *>>> computeLoops(std::vector<Subgraph> & partitions);

