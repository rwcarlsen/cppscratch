
#include "graph.h"
#include "show.h"

int Node::_n_visits = 0;

int Subgraph::_next_id = 0;

bool canMerge(Node * a, Node * b)
{
  // this allows us to consider all Elemental_foo loop types mergeable
  static std::map<LoopCategory, std::set<LoopCategory>> mergeable = {
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
  if (a->loopType().block != b->loopType().block)
    return false;
  if (a->dependsOn(b) || b->dependsOn(a))
    return false;
  return true;
}

Node::Node(Graph * g, const std::string & name, bool cached, bool reducing, LoopType l)
    : _owner(g), _name(name), _cached(cached), _reducing(reducing), _looptype(l)
{
}

std::set<Node *> Node::deps() const { return _deps; }
std::set<Node *> Node::dependers() const { return _dependers; }

bool Node::dependsOn(Node * n)
{
  return n->_transitive_dependers.count(this) > 0;
}
void Node::transitiveDependers(std::set<Node *> & all) const
{
  all.insert(_transitive_dependers.begin(), _transitive_dependers.end());
}

void Node::transitiveDeps(std::set<Node *> & all) const
{
  for (auto d : _deps)
  {
    if (all.count(d) > 0)
      continue;
    all.insert(d);
    d->transitiveDeps(all);
  }
}

bool Node::isReducing() const { return _reducing; }
bool Node::isCached() const { return _cached || _reducing; }
LoopType Node::loopType() const { return _looptype; }

std::string Node::str() { return _name; }
std::string Node::name() { return _name; }

void Node::clearDeps() {_deps.clear(); _dependers.clear();}

int Node::id() {return _id;}

void Node::setId(int id)
{
  if (_id != -1)
    throw std::runtime_error("setting node id multiple times");
  if (id == -1)
    throw std::runtime_error("cannot set node id to -1");
  _id = id;
}

int Node::loop()
{
  if (_loop == -1)
    _loop = loopInner();
  return _loop;
}

void
Node::prepare()
{
  auto & store = _owner->storage();
  for (int i = 0; i < store.size(); i++)
    store[i]->_loop = -1;

  for (auto r : _owner->roots())
    r->loop();
}


void Node::inheritDependers(Node * n, std::set<Node *> & dependers)
{
  assert(dependers.count(this) == 0);
  if (n->_visit_count == _n_visits)
    return;
  n->_visit_count = _n_visits;

  _transitive_dependers.insert(n);
  _transitive_dependers.insert(dependers.begin(), dependers.end());
  assert(_transitive_dependers.count(this) == 0);

  for (auto d : _deps)
    d->inheritDependers(n, dependers);
}

int Node::loopInner()
{
  if (_dependers.size() == 0)
    return 0;

  assert(_transitive_dependers.count(this) == 0);

  auto depiter = _dependers.begin();
  int maxloop = (*depiter)->loop();
  for (; depiter != _dependers.end(); depiter++)
  {
    auto dep = *depiter;
    // TODO: does the loop number really need to increment if the loop type
    // changes - is this the right logic?
    auto deploop = dep->loop();
    if (dep->loopType() != loopType() || isReducing())
    {
      if (deploop + 1 > maxloop)
        maxloop = deploop + 1;
    }
    else
    {
      if (deploop > maxloop)
        maxloop = deploop;
    }
  }
  return maxloop;
}

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
    // TODO: this mapping is not quite right.  It is possible (because of
    // uncached nodes) that a given node n can exist in multiple partitions -
    // when this is the case, the node to loopnode mapping here will be
    // overwritten - the last partition having that node "wins" and the
    // mapping will point to that loop_node/partition.  This causes the
    // partitions for which this node mapping was overwritten to have missing
    // loop/partition dependencies. We need to fix this to make the map key be
    // partition and node specific - not just keyed off of node pointers.
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

  graphgraph.prepare();

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
      if (loop1 == other2 || loop1->dependsOn(other2) || other2->dependsOn(loop1))
      {
        auto tmp = other1;
        other1 = other2;
        other2 = tmp;
      }

      if (loop1->dependsOn(other1) && other2->dependsOn(loop2))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (other1->dependsOn(loop1) && loop2->dependsOn(other2))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (loop1 == other1 && (loop2->dependsOn(other2) || other2->dependsOn(loop2)))
      {
        cancellations[i].push_back(j);
        cancellations[j].push_back(i);
      }
      else if (loop2 == other2 && (loop1->dependsOn(other1) || other1->dependsOn(loop1)))
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

  return splits;
}

std::vector<Subgraph>
computePartitions(Graph & g, bool merge)
{
  g.prepare();

  std::vector<Subgraph> partitions;

  int maxloop = 0;
  // start at all the leaf nodes - i.e. nodes that have "no" dependencies -
  // i.e. things that are either output data or residuals.

  // start at all the root nodes - i.e. things that came from a previous time
  // step or that come from the ether - i.e. solution/variable-values, cached
  // values, etc.  Find the max loop number (most deep in dep tree)
  for (auto n : g.roots())
  {
    auto loop = n->loop();
    if (loop > maxloop)
      maxloop = loop;
  }

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

