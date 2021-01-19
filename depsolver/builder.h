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
  throw std::runtime_error("node not found");
}

void
addNode(TransitionMatrix & m, const std::string & base_name, LoopCategory cat, int block, bool cached, bool reducing)
{
  auto name = nodeName(base_name, block, cat);
  Node * n = m.graph.create(name, cached, reducing, LoopType(cat, block));
  m.candidate_blocks[base_name].insert(block);
  m.candidate_cats[base_name].insert(cat);
  if (reducing)
    m.candidate_reducing.insert(base_name);
  if (cached)
    m.candidate_cached.insert(base_name);
  m.candidates[base_name].push_back(n);
}

void
bindDep(TransitionMatrix & m, const std::string & node_base, const std::string & dep_base)
{
  for (auto cat : m.candidate_cats[node_base])
  {
    auto dstcat = cat;
    if (m.candidate_cats[dep_base].count(cat) == 0)
    {
      // the dependency does not have the matching category
      // so it must be a cached reducing node - enforce this
      if (m.candidate_reducing.count(dep_base) == 0 || m.candidate_cached.count(dep_base) == 0)
        throw std::runtime_error("cannot bind to a dependency with differing loop category that isn't both reducing and cached");
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
          srcnode->needs(getNode(m, dep_base, dstcat, depblock));
      else
        // depend on the same dep block as each src block
        srcnode->needs(getNode(m, dep_base, dstcat, srcblock));
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
      // so it must be a cached reducing node - enforce this
      if (m.candidate_reducing.count(dep_base) == 0 || m.candidate_cached.count(dep_base) == 0)
        throw std::runtime_error("cannot transition to a dependency with differing loop category that isn't both reducing and cached");
      if (m.candidate_cats[dep_base].size() > 1)
        throw std::runtime_error("cannot transition to a dependency with differing loop category that has nodes in multiple categories");
      dstcat = *(m.candidate_cats[dep_base].begin());
    }

    // the dependency has the matching category
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

void
walkTransitions(TransitionMatrix & m, std::set<Node *> stack, Node * n)
{
  auto & deps_map = m.matrix[n];

  // choose a dependency
  if (deps_map.size() == 0)
    return;

  std::uniform_real_distribution<double> unif(0, 1);
  std::default_random_engine re;
  double r = unif(re);

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
      if (stack.count(dep) > 0)
        break;
      n->needs(dep);
      stack.insert(dep);
      walkTransitions(m, stack, dep);
      stack.erase(dep);
    }
    break;
  }
}

void
buildGraph(TransitionMatrix & m, int n_paths_from_each_leaf)
{
  for (auto leaf : m.graph.leaves())
    for (int i = 0; i < n_paths_from_each_leaf; i++)
      walkTransitions(m, {}, leaf);
}

void buildTransitionMatrix(TransitionMatrix & m)
{
  std::vector<LoopCategory> elemental = {LoopCategory::Elemental_onElem};
  std::vector<LoopCategory> nodal = {LoopCategory::Nodal};
  std::vector<int> blocks = {1, 2, 3, 4, 5};

  // vars can be calc'd/used anywhere - any loop type
  generateNodes(m, "Var1", false, false, blocks);
  generateNodes(m, "Var2", false, false, blocks);

  generateNodes(m, "Kernel1", false, false, blocks, elemental);
  generateNodes(m, "Kernel2", false, false, blocks, elemental);
  generateNodes(m, "Kernel3", false, false, blocks, elemental);
  generateNodes(m, "Kernel4", false, false, {1, 2, 3, 4}, elemental);
  generateNodes(m, "Kernel5", false, false, blocks, elemental);
  generateNodes(m, "BC1", false, false, blocks, elemental);
  generateNodes(m, "BC2", false, false, blocks, elemental);
  generateNodes(m, "BC3", false, false, blocks, elemental);
  generateNodes(m, "BC4", false, false, blocks, elemental);
  generateNodes(m, "BC5", false, false, blocks, elemental);

  generateNodes(m, "Solution", true, true, blocks, allCats());
  generateNodes(m, "Damper1", false, false, blocks, nodal);
  generateNodes(m, "FinalSolution", true, false, blocks, nodal);

  // auxvars can be calc'd/used anywhere - any loop type - as long as they are nodal
  generateNodes(m, "AuxVar1", false, false, blocks);
  generateNodes(m, "AuxVar2", false, false, blocks);
  generateNodes(m, "AuxKernel1", false, false, blocks, nodal);
  generateNodes(m, "AuxKernel2", false, false, blocks, nodal);

  generateNodes(m, "AuxSolution", true, true, blocks, allCats());

  generateNodes(m, "Material1", false, false, blocks);
  generateNodes(m, "Material2", false, false, blocks);
  generateNodes(m, "Material3", false, false, blocks);
  generateNodes(m, "Postprocessor1", true, true, blocks);
  generateNodes(m, "Output1", false, true, blocks);

  generateNodes(m, "Marker", true, false, blocks, elemental);
  generateNodes(m, "Indicator", true, false, blocks);
  generateNodes(m, "Mesh", true, true, blocks, elemental);

  // every kernel and bc must depend on a primary variable
  bindDep(m, "Kernel1", "Var1");
  bindDep(m, "Kernel2", "Var1");
  bindDep(m, "Kernel3", "Var2");
  bindDep(m, "Kernel4", "Var2");
  bindDep(m, "Kernel5", "Var3");
  bindDep(m, "BC1", "Var1");
  bindDep(m, "BC2", "Var1");
  bindDep(m, "BC3", "Var2");
  bindDep(m, "BC4", "Var3");
  bindDep(m, "BC5", "Var3");
  // variables must depend on mesh
  bindDep(m, "Var1", "Mesh");
  bindDep(m, "Var2", "Mesh");
  bindDep(m, "Var3", "Mesh");
  // solution depends on all kernels and bcs
  bindDep(m, "Solution", "Kernel1");
  bindDep(m, "Solution", "Kernel2");
  bindDep(m, "Solution", "Kernel3");
  bindDep(m, "Solution", "Kernel4");
  bindDep(m, "Solution", "Kernel5");
  bindDep(m, "Solution", "BC1");
  bindDep(m, "Solution", "BC2");
  bindDep(m, "Solution", "BC3");
  bindDep(m, "Solution", "BC4");
  bindDep(m, "Solution", "BC5");

  addTransition(m, "Kernel1", "Material1", 0.6);
  addTransition(m, "Kernel1", "Material2", 0.3);
  addTransition(m, "Kernel1", "Material3", 0.1);
}

