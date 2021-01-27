#pragma once

#include "graph.h"
#include "show.h"

#include <random>
#include <algorithm>


class Kernel : public Node
{
public:
  Kernel(std::string name, LoopCategory cat, int block) : Node(name, false, false, LoopType(cat, block)) {}
};

std::string
nodeName(const std::string base_name, int block, LoopCategory cat)
{
  return base_name + "_" + loopCategoryStr(cat) + "_block" + std::to_string(block);
}

std::string
nodeName(Node * n)
{
  return n->name() + "_" + loopCategoryStr(n->loopType().category) + "_block" + std::to_string(n->loopType().block);
}

struct Transitions
{
  std::vector<std::vector<Node *>> dependencies;
  std::vector<double> probabilities;
};

struct TransitionMatrix
{
  Graph graph;
  // map base name to candidate
  std::map<std::string, std::vector<Node *>> candidates;;

  std::map<Node *, std::map<std::vector<Node *>, double>> matrix;

  std::map<std::string, std::set<int>> candidate_blocks;
  std::map<std::string, std::set<LoopCategory>> candidate_cats;
  std::set<std::string> candidate_reducing;
  std::set<std::string> candidate_cached;
};

bool
haveNode(TransitionMatrix & m, const std::string & base_name, LoopCategory cat, int block)
{
  LoopType want(cat, block);
  auto & nodes = m.candidates[base_name];
  for (auto n : nodes)
  {
    if (want == n->loopType())
      return true;
  }
  return false;
}

Node *
getNode(TransitionMatrix & m, const std::string & base_name, LoopCategory cat, int block)
{
  LoopType want(cat, block);
  auto & nodes = m.candidates[base_name];
  for (auto n : nodes)
  {
    if (want == n->loopType())
      return n;
  }
  throw std::runtime_error("node \"" + nodeName(base_name, block, cat) + "\" not found");
}

void
addNode(TransitionMatrix & m, const std::string & base_name, LoopCategory cat, int block, bool cached, bool reducing)
{
  Node * n = m.graph.create(base_name, cached, reducing, LoopType(cat, block));
  m.candidate_blocks[base_name].insert(block);
  m.candidate_cats[base_name].insert(cat);
  if (reducing)
    m.candidate_reducing.insert(base_name);
  if (cached)
    m.candidate_cached.insert(base_name);
  m.candidates[base_name].push_back(n);
}

void
bindDep(TransitionMatrix & m, const std::string & node_base, const std::string & dep_base, bool allow_missing_dep_blocks = false)
{
  if (m.candidate_cats.count(node_base) == 0)
    throw std::runtime_error("cannot bind non-existing dependency \"" + node_base + "\" to a dependency");
  if (m.candidate_cats.count(dep_base) == 0)
    throw std::runtime_error("cannot bind node to non-existing dependency \"" + dep_base + "\"");
  for (auto cat : m.candidate_cats[node_base])
  {
    auto dstcat = cat;
    if (m.candidate_cats[dep_base].count(cat) == 0)
    {
      // the dependency does not have the matching category
      // so it must be a cached node - enforce this
      if (m.candidate_cached.count(dep_base) == 0)
        throw std::runtime_error("cannot bind to a dependency with differing loop category that isn't cached");
      if (m.candidate_cats[dep_base].size() > 1)
        throw std::runtime_error("cannot bind to a dependency with differing loop category that has nodes in multiple categories");
      dstcat = *(m.candidate_cats[dep_base].begin());
    }

    // the dependency has the matching category
    for (auto srcblock : m.candidate_blocks[node_base])
    {
      auto srcnode = getNode(m, node_base, cat, srcblock);
      if(m.candidate_reducing.count(dep_base) > 0)
        // depend on all dep blocks for each src block
        for (auto depblock : m.candidate_blocks[dep_base])
        {
          auto dep = getNode(m, dep_base, dstcat, depblock);
          if (srcnode->isDepender(dep))
            continue;
          srcnode->needs(dep);
        }
      else
      {
        // depend on the same dep block as each src block
        if (haveNode(m, dep_base, dstcat, srcblock))
        {
          auto dep = getNode(m, dep_base, dstcat, srcblock);
          if (srcnode->isDepender(dep))
            continue;
          srcnode->needs(dep);
        }
        else if (!allow_missing_dep_blocks)
          std::runtime_error("cannot bind node " + nodeName(node_base, srcblock, cat) + " to dependency " + dep_base + " not defined on block " + std::to_string(srcblock));
      }
    }
  }
}

void
addTransition(TransitionMatrix & m, const std::string & node_base, const std::string & dep_base, double probability)
{
  for (auto cat : m.candidate_cats[node_base])
  {
    auto dstcat = cat;
    if (m.candidate_cats[dep_base].count(cat) == 0)
    {
      // the dependency does not have the matching category
      // so it must be a cached node - enforce this
      if (m.candidate_cached.count(dep_base) == 0)
        throw std::runtime_error("cannot transition to a dependency with differing loop category that isn't cached");
      if (m.candidate_cats[dep_base].size() > 1)
        throw std::runtime_error("cannot transition to a dependency with differing loop category that has nodes in multiple categories");
      dstcat = *(m.candidate_cats[dep_base].begin());
    }

    for (auto srcblock : m.candidate_blocks[node_base])
    {
      auto srcnode = getNode(m, node_base, cat, srcblock);
      std::vector<Node *> dstnodes;
      if(m.candidate_reducing.count(dep_base) > 0)
      {
        // depend on all dep blocks for each src block
        for (auto depblock : m.candidate_blocks[dep_base])
          dstnodes.push_back(getNode(m, dep_base, dstcat, depblock));
      }
      else
      {
        // depend on the same dep block as each src block
        dstnodes.push_back(getNode(m, dep_base, dstcat, srcblock));
      }
      m.matrix[srcnode][dstnodes] = probability;
    }
  }
}

std::vector<LoopCategory> allCats()
{
  return {LoopCategory::Nodal, LoopCategory::Face, LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide};
}

void
generateNodes(TransitionMatrix & m, const std::string & base_name, bool cached, bool reducing, const std::vector<int> & blocks = {}, std::vector<LoopCategory> cats = {})
{
  if (blocks.size() == 0 && reducing)
    throw std::runtime_error("cannot have a reducing node operating in no blocks");

  if (blocks.size() == 0)
  {
    addNode(m, base_name, LoopCategory::None, 0, cached, reducing);
    return;
  }

  if (cats.size() == 0)
    cats = allCats();
  for (auto block : blocks)
    for (int j = 0; j < cats.size(); j++)
      addNode(m, base_name, cats[j], block, cached, reducing);
}

// If sync_blocks is true, then all nodes of with the same name as n (i.e.
// nodes in every block with name n) will get the same dependencies as each
// other.
void
walkTransitions(TransitionMatrix & m, std::default_random_engine & engine, Node * n, bool sync_blocks)
{
  auto & deps_map = m.matrix[n];

  // choose a dependency
  if (deps_map.size() == 0)
    return;

  std::uniform_real_distribution<double> unif(0, 1);
  double r = unif(engine);

  double prob_sum = 0;
  for (auto it : deps_map)
  {
    auto & deps = it.first;
    auto & prob = it.second;
    prob_sum += prob;
    if (r > prob_sum)
      continue;

    for (auto dep : deps)
    {
      // skip/disallow cyclical deps
      if (n->isDepender(dep))
        break;
      if (sync_blocks)
        bindDep(m, n->name(), dep->name());
      else
        n->needs(dep);
    }
    // we doo all the needs/dep-setting calls first at once - to ensure
    // breadth-first generation - this prevents weird dependency conflicts
    // relating to avoiding cyclical dependencies and depending on reducing
    // nodes.
    for (auto dep : deps)
      walkTransitions(m, engine, dep, sync_blocks);
    break;
  }
}

void
buildGraph(TransitionMatrix & m, Node * start, int n_walks, bool sync_blocks)
{
  std::default_random_engine re;
  for (int i = 0; i < n_walks; i++)
    walkTransitions(m, re, start, sync_blocks);
}

// returns the "start"/master node
Node *
buildTransitionMatrix(TransitionMatrix & m)
{
  std::vector<LoopCategory> elemental = {LoopCategory::Elemental_onElem};
  std::vector<LoopCategory> nodal = {LoopCategory::Nodal};
  std::vector<int> blocks = {1, 2, 3, 4, 5};

  // vars can be calc'd/used anywhere - any loop type
  generateNodes(m, "Var1", false, false, blocks);
  generateNodes(m, "Var2", false, false, blocks);

  generateNodes(m, "Kernel1", true, true, blocks, elemental);
  generateNodes(m, "Kernel2", true, true, blocks, elemental);
  generateNodes(m, "Kernel3", true, true, blocks, elemental);
  generateNodes(m, "BC1", true, true, blocks, elemental);
  generateNodes(m, "BC2", true, true, blocks, elemental);
  generateNodes(m, "BC3", true, true, blocks, elemental);

  generateNodes(m, "Solution", true, false);
  generateNodes(m, "Damper1", true, true, blocks, nodal);
  generateNodes(m, "FinalSolution", true, false);

  // auxvars can be calc'd/used anywhere - any loop type - as long as they are nodal
  generateNodes(m, "AuxVar1", false, false, blocks);
  generateNodes(m, "AuxVar2", false, false, blocks);
  generateNodes(m, "AuxKernel1", true, true, blocks, nodal);
  generateNodes(m, "AuxKernel2", true, true, blocks, nodal);

  generateNodes(m, "AuxSolution", true, false, blocks, allCats());

  generateNodes(m, "Material1", false, false, blocks);
  generateNodes(m, "Material2", false, false, blocks);
  generateNodes(m, "Material3", false, false, blocks);
  generateNodes(m, "Postprocessor1", true, true, blocks, elemental);
  generateNodes(m, "Postprocessor2", true, true, blocks, nodal);
  generateNodes(m, "Output1", false, true, blocks);

  generateNodes(m, "Marker", true, false, blocks, elemental);
  generateNodes(m, "Indicator", true, false, blocks);
  generateNodes(m, "Mesh", true, true, blocks, elemental);

  // every kernel and bc must depend on a primary variable
  bindDep(m, "Kernel1", "Var1");
  bindDep(m, "Kernel2", "Var1");
  bindDep(m, "Kernel3", "Var2");
  bindDep(m, "BC1", "Var1");
  bindDep(m, "BC2", "Var1");
  bindDep(m, "BC3", "Var1");
  // variables must depend on mesh
  bindDep(m, "Var1", "Mesh");
  bindDep(m, "Var2", "Mesh");
  // solution depends on all kernels and bcs
  bindDep(m, "Solution", "Kernel1");
  bindDep(m, "Solution", "Kernel2");
  bindDep(m, "Solution", "Kernel3");
  bindDep(m, "Solution", "BC1");
  bindDep(m, "Solution", "BC2");
  bindDep(m, "Solution", "BC3");
  bindDep(m, "FinalSolution", "Solution");
  // aux deps
  bindDep(m, "AuxSolution", "AuxKernel1");
  bindDep(m, "AuxSolution", "AuxKernel2");

  // these transitions must mirror the bound/forced dependencies
  addTransition(m, "FinalSolution", "Solution", 1);
  addTransition(m, "Solution", "Kernel1", 0.2);
  addTransition(m, "Solution", "Kernel2", 0.2);
  addTransition(m, "Solution", "Kernel3", 0.2);
  addTransition(m, "Solution", "BC1", 0.2);
  addTransition(m, "Solution", "BC2", 0.1);
  addTransition(m, "Solution", "BC3", 0.1);
  addTransition(m, "AuxSolution", "AuxKernel1", 0.4);
  addTransition(m, "AuxSolution", "AuxKernel2", 0.4);

  addTransition(m, "AuxKernel1", "Material1", 0.1);
  addTransition(m, "AuxKernel1", "Material2", 0.1);
  addTransition(m, "AuxKernel1", "Material3", 0.1);
  addTransition(m, "AuxKernel1", "Postprocessor1", 0.1);
  addTransition(m, "AuxKernel1", "Postprocessor2", 0.1);
  addTransition(m, "AuxKernel1", "AuxVar1", 0.1);
  addTransition(m, "AuxKernel1", "AuxVar2", 0.1);
  addTransition(m, "AuxKernel1", "Var1", 0.1);
  addTransition(m, "AuxKernel1", "Var2", 0.1);

  addTransition(m, "AuxKernel2", "Material1", 0.1);
  addTransition(m, "AuxKernel2", "Material2", 0.1);
  addTransition(m, "AuxKernel2", "Material3", 0.1);
  addTransition(m, "AuxKernel2", "Postprocessor1", 0.1);
  addTransition(m, "AuxKernel2", "Postprocessor2", 0.1);
  addTransition(m, "AuxKernel2", "AuxVar1", 0.1);
  addTransition(m, "AuxKernel2", "AuxVar2", 0.1);
  addTransition(m, "AuxKernel2", "Var1", 0.1);
  addTransition(m, "AuxKernel2", "Var2", 0.1);

  addTransition(m, "Kernel1", "Material1", 0.1);
  addTransition(m, "Kernel1", "Material2", 0.1);
  addTransition(m, "Kernel1", "Material3", 0.1);
  addTransition(m, "Kernel1", "Postprocessor1", 0.1);
  addTransition(m, "Kernel1", "Postprocessor2", 0.1);
  addTransition(m, "Kernel1", "AuxVar1", 0.1);
  addTransition(m, "Kernel1", "AuxVar2", 0.1);
  addTransition(m, "Kernel1", "Var1", 0.1);
  addTransition(m, "Kernel1", "Var2", 0.1);

  addTransition(m, "Kernel2", "Material1", 0.1);
  addTransition(m, "Kernel2", "Material2", 0.1);
  addTransition(m, "Kernel2", "Material3", 0.1);
  addTransition(m, "Kernel2", "Postprocessor1", 0.1);
  addTransition(m, "Kernel2", "Postprocessor2", 0.1);
  addTransition(m, "Kernel2", "AuxVar1", 0.1);
  addTransition(m, "Kernel2", "AuxVar2", 0.1);
  addTransition(m, "Kernel2", "Var1", 0.1);
  addTransition(m, "Kernel2", "Var2", 0.1);

  addTransition(m, "Kernel3", "Material1", 0.1);
  addTransition(m, "Kernel3", "Material2", 0.1);
  addTransition(m, "Kernel3", "Material3", 0.1);
  addTransition(m, "Kernel3", "Postprocessor1", 0.1);
  addTransition(m, "Kernel3", "Postprocessor2", 0.1);
  addTransition(m, "Kernel3", "AuxVar1", 0.1);
  addTransition(m, "Kernel3", "AuxVar2", 0.1);
  addTransition(m, "Kernel3", "Var1", 0.1);
  addTransition(m, "Kernel3", "Var2", 0.1);

  addTransition(m, "BC1", "Material1", 0.1);
  addTransition(m, "BC1", "Material2", 0.1);
  addTransition(m, "BC1", "Material3", 0.1);
  addTransition(m, "BC1", "Postprocessor1", 0.1);
  addTransition(m, "BC1", "Postprocessor2", 0.1);
  addTransition(m, "BC1", "AuxVar1", 0.1);
  addTransition(m, "BC1", "AuxVar2", 0.1);
  addTransition(m, "BC1", "Var1", 0.1);
  addTransition(m, "BC1", "Var2", 0.1);

  addTransition(m, "BC2", "Material1", 0.1);
  addTransition(m, "BC2", "Material2", 0.1);
  addTransition(m, "BC2", "Material3", 0.1);
  addTransition(m, "BC2", "Postprocessor1", 0.1);
  addTransition(m, "BC2", "Postprocessor2", 0.1);
  addTransition(m, "BC2", "AuxVar1", 0.1);
  addTransition(m, "BC2", "AuxVar2", 0.1);
  addTransition(m, "BC2", "Var1", 0.1);
  addTransition(m, "BC2", "Var2", 0.1);

  addTransition(m, "BC3", "Material1", 0.1);
  addTransition(m, "BC3", "Material2", 0.1);
  addTransition(m, "BC3", "Material3", 0.1);
  addTransition(m, "BC3", "Postprocessor1", 0.1);
  addTransition(m, "BC3", "Postprocessor2", 0.1);
  addTransition(m, "BC3", "AuxVar1", 0.1);
  addTransition(m, "BC3", "AuxVar2", 0.1);
  addTransition(m, "BC3", "Var1", 0.1);
  addTransition(m, "BC3", "Var2", 0.1);

  addTransition(m, "Postprocessor1", "Material1", 0.1);
  addTransition(m, "Postprocessor1", "Material2", 0.1);
  addTransition(m, "Postprocessor1", "Material3", 0.1);
  addTransition(m, "Postprocessor1", "AuxVar1", 0.1);
  addTransition(m, "Postprocessor1", "AuxVar2", 0.1);
  addTransition(m, "Postprocessor1", "Var1", 0.1);
  addTransition(m, "Postprocessor1", "Var2", 0.1);
  addTransition(m, "Postprocessor1", "Postprocessor2", 0.1);

  addTransition(m, "Postprocessor2", "Material1", 0.1);
  addTransition(m, "Postprocessor2", "Material2", 0.1);
  addTransition(m, "Postprocessor2", "Material3", 0.1);
  addTransition(m, "Postprocessor2", "AuxVar1", 0.1);
  addTransition(m, "Postprocessor2", "AuxVar2", 0.1);
  addTransition(m, "Postprocessor2", "Var1", 0.1);
  addTransition(m, "Postprocessor2", "Var2", 0.1);
  addTransition(m, "Postprocessor2", "Postprocessor1", 0.1);

  addTransition(m, "Material1", "Postprocessor1", 0.1);
  addTransition(m, "Material1", "Postprocessor2", 0.1);
  addTransition(m, "Material1", "Var1", 0.1);
  addTransition(m, "Material1", "Var2", 0.1);
  addTransition(m, "Material1", "Material2", 0.1);
  addTransition(m, "Material1", "Material3", 0.1);
  addTransition(m, "Material1", "AuxVar1", 0.1);
  addTransition(m, "Material1", "AuxVar2", 0.1);

  addTransition(m, "Material2", "Postprocessor1", 0.1);
  addTransition(m, "Material2", "Postprocessor2", 0.1);
  addTransition(m, "Material2", "Var1", 0.1);
  addTransition(m, "Material2", "Var2", 0.1);
  addTransition(m, "Material2", "Material1", 0.1);
  addTransition(m, "Material2", "Material3", 0.1);
  addTransition(m, "Material2", "AuxVar1", 0.1);
  addTransition(m, "Material2", "AuxVar2", 0.1);

  addTransition(m, "Material3", "Postprocessor1", 0.1);
  addTransition(m, "Material3", "Postprocessor2", 0.1);
  addTransition(m, "Material3", "Var1", 0.1);
  addTransition(m, "Material3", "Var2", 0.1);
  addTransition(m, "Material3", "Material1", 0.1);
  addTransition(m, "Material3", "Material2", 0.1);
  addTransition(m, "Material3", "AuxVar1", 0.1);
  addTransition(m, "Material3", "AuxVar2", 0.1);
  return getNode(m, "FinalSolution", LoopCategory::None, 0);
}

