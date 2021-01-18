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

struct Candidate
{
  Node * node;
  std::vector<std::string> dependencies;
  std::vector<double> probabilities;
};

struct TransitionMatrix
{
  Graph graph;
  std::map<std::string, Candidate> matrix;
};

void
addNode(TransitionMatrix & m, const std::string & name, LoopCategory cat, int block, bool cached, bool reducing)
{
  Node * n = m.graph.create(name, cached, reducing, LoopType(cat, block));
  m.matrix[name] = {n, {}, {}};
}

void
addTransition(TransitionMatrix & m, const std::string & from, const std::string & to, double probability)
{
  if (m.matrix.count(from) == 0)
    throw std::runtime_error("cannot add transition for non-existing graph node " + from);

  auto & deps = m.matrix[from].dependencies;
  if (std::find(deps.begin(), deps.end(), to) != deps.end())
    throw std::runtime_error("node " + from + " already has transition to node " + to);

  deps.push_back(to);
  m.matrix[from].probabilities.push_back(probability);
}

void
generateNodes(TransitionMatrix & m, const std::string & base_name, bool cached, bool reducing, int nblocks)
{
  std::vector<LoopCategory> cats = { LoopCategory::None, LoopCategory::Nodal, LoopCategory::Face, LoopCategory::Elemental_onElem, LoopCategory::Elemental_onElemFV, LoopCategory::Elemental_onBoundary, LoopCategory::Elemental_onInternalSide};
  for (int i = 1; i < nblocks+1; i++)
    for (int j = 0; j < cats.size(); j++)
      addNode(m, base_name + "_" + loopCategoryStr(cats[j]) + "_block" + std::to_string(i), cats[j], i, cached, reducing);
}

// Foo_None_block1
// Foo_Nodal_block1
// Foo_Face_block1
// Foo_Elemental_onElem_block1
// Foo_Elemental_onElemFV_block1
// Foo_Elemental_onBoundary_block1
// Foo_Elemental_onInternalSide_block1

void buildTransitionMatrix(TransitionMatrix & m)
{
  generateNodes(m, "Var1", false, false, 0);
  generateNodes(m, "Var2", false, false, 0);
  generateNodes(m, "Var3", false, false, 0);
  generateNodes(m, "Var4", false, false, 0);
  generateNodes(m, "Var5", false, false, 0);
  generateNodes(m, "Kernel1", false, false, 5);
  generateNodes(m, "Kernel2", false, false, 5);
  generateNodes(m, "Kernel3", false, false, 5);
  generateNodes(m, "Kernel4", false, false, 5);
  generateNodes(m, "Kernel5", false, false, 5);
  generateNodes(m, "BC1", false, false, 5);
  generateNodes(m, "BC2", false, false, 5);
  generateNodes(m, "BC3", false, false, 5);
  generateNodes(m, "BC4", false, false, 5);
  generateNodes(m, "BC5", false, false, 5);
  generateNodes(m, "Damper1", false, false, 0);
  generateNodes(m, "Damper2", false, false, 0);
  generateNodes(m, "Damper3", false, false, 0);
  generateNodes(m, "Damper4", false, false, 0);
  generateNodes(m, "Damper5", false, false, 0);

  generateNodes(m, "Solution", true, true, 0);
  generateNodes(m, "FinalSolution", true, false, 0);

  generateNodes(m, "AuxVar1", false, false, 0);
  generateNodes(m, "AuxVar2", false, false, 0);
  generateNodes(m, "AuxVar3", false, false, 0);
  generateNodes(m, "AuxVar4", false, false, 0);
  generateNodes(m, "AuxVar5", false, false, 0);
  generateNodes(m, "AuxKernel1", false, false, 0);
  generateNodes(m, "AuxKernel2", false, false, 0);
  generateNodes(m, "AuxKernel3", false, false, 0);
  generateNodes(m, "AuxKernel4", false, false, 0);
  generateNodes(m, "AuxKernel5", false, false, 0);

  generateNodes(m, "AuxSolution", true, false, 0);

  generateNodes(m, "Material1", false, false, 5);
  generateNodes(m, "Material2", false, false, 5);
  generateNodes(m, "Material3", false, false, 5);
  generateNodes(m, "Material4", false, false, 5);
  generateNodes(m, "Material5", false, false, 5);
  generateNodes(m, "Postprocessor1", true, true, 0);
  generateNodes(m, "Postprocessor2", true, true, 0);
  generateNodes(m, "Postprocessor3", true, true, 0);
  generateNodes(m, "Postprocessor4", true, true, 0);
  generateNodes(m, "Postprocessor5", true, true, 0);
  generateNodes(m, "Output1", false, true, 0);
  generateNodes(m, "Output2", false, true, 0);
  generateNodes(m, "Output3", false, true, 0);
  generateNodes(m, "Output4", false, true, 0);
  generateNodes(m, "Output5", false, true, 0);

  generateNodes(m, "Marker", true, false, 0);
  generateNodes(m, "Indicator", true, false, 0);
  generateNodes(m, "Mesh", true, true, 0);

  addTransition(m, "Kernel1_Elemental_onElem_block1", "Material3_Elemental_onElem_block1", 0.5);
  addTransition(m, "Solution", "Kernel1_Elemental_onElem_block1", 0.5);
}

void
walkTransitions(TransitionMatrix & m, std::set<Node *> stack, Node * n)
{
  auto & deps = m.matrix[n->name()].dependencies;
  auto & probs = m.matrix[n->name()].probabilities;

  // choose a dependency
  if (deps.size() == 0)
    return;

  std::uniform_real_distribution<double> unif(0, 1);
  std::default_random_engine re;
  double r = unif(re);

  double prob_sum = 0;
  for (int i = 0; i < probs.size(); i++)
  {
    auto dep = m.matrix[deps[i]].node;
    prob_sum += probs[i];
    if (r > prob_sum || stack.count(dep) > 0) // skip/disallow cyclical deps
      continue;

    stack.insert(dep);
    n->needs(dep);
    walkTransitions(m, stack, dep);
    stack.erase(dep);
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

