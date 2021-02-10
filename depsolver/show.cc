
#include "show.h"

#include <string>
#include <vector>
#include <sstream>
#include <iostream>

std::string
loopCategoryStr(const LoopCategory cat)
{
  if (cat == LoopCategory::None)
    return "None";
  else if (cat == LoopCategory::Nodal)
    return "Nodal";
  else if (cat == LoopCategory::Face)
    return "Face";
  else if (cat == LoopCategory::Elemental_onElem)
    return "Elemental_onElem";
  else if (cat == LoopCategory::Elemental_onElemFV)
    return "Elemental_onElemFV";
  else if (cat == LoopCategory::Elemental_onBoundary)
    return "Elemental_onBoundary";
  else if (cat == LoopCategory::Elemental_onInternalSide)
    return "Elemental_onInternalSide";
  return "UNKNOWN";
}

std::string
loopTypeStr(const LoopType & l)
{
  std::string s;
  s += loopCategoryStr(l.category);
  s += ":block" + std::to_string(l.block);
  return s;
}

std::string
nodeLabel(const Subgraph & g, Node * n)
{
  std::string s = n->str() + " on partition " + std::to_string(g.id()) + "\\n";
  s += loopTypeStr(n->loopType());
  if (n->isCached() || n->isReducing())
  {
    s += "\\n(";
    if (n->isCached())
    {
      s += "cached";
      if (n->isReducing())
        s += ",";
    }
    if (n->isReducing())
      s += "reducing";
    s += ")";
  }
  return s;
}

// outputs an a dot script edge pointing from src to dst.  If dst is nullptr,
// then src is created as an "island" node.  If dst is not contained in the
// subgraph g, then the node is filled with an off-yellow color - indicating
// that this node represents a value that is cached and should already be
// available (i.e. computed in a prior loop) - filled nodes are not
// (re)calculated in the current loop, just (re)used.
std::string
dotEdge(const Subgraph & g, Node * src, Node * dst)
{
  if (dst)
  {
    if (g.contains(dst))
      return "\"" + nodeLabel(g, src) + "\" -> \"" + nodeLabel(g, dst) + "\";\n";
    std::string dstlabel = "\"" + nodeLabel(g, dst) + "\"";
    return "\"" + nodeLabel(g, src) + "\" -> " + dstlabel + ";\n" + dstlabel  + " [style=filled, fillcolor=khaki];\n";
  }
  return "\"" + nodeLabel(g, src) + "\";\n";
}

std::string
dotConnections(const Subgraph & g)
{
  std::stringstream ss;
  for (auto n : g.nodes())
  {
    bool island = true;
    for (auto dep : n->deps())
    {
      island = false;
      ss << dotEdge(g, n, dep);
    }
    for (auto dep : n->dependers())
      if (g.contains(dep))
        island = false;

    if (island)
      ss << dotEdge(g, n, nullptr);
  }
  return ss.str();
}

// show all the given subgraphs on a single graph.
std::string
dotGraphMerged(const std::vector<Subgraph> & graphs)
{
  std::stringstream ss;
  ss << "digraph g {\n";
  int count = 0;
  for (auto & g : graphs)
  {
    count++;
    ss << "subgraph g" << count << "{\n";
    ss << dotConnections(g);
    ss << "}\n";
  }
  ss << "}\n";
  return ss.str();
}

std::string
dotGraph(const Subgraph & g)
{
  std::stringstream ss;
  ss << "digraph g {\n";
  ss << dotConnections(g);
  ss << "}\n";
  return ss.str();
}

// Note that the loop labeling may be partially wrong/incomplete for cases
// where we merged similar loop categories together - e.g. when merging nodes
// from Elemental_onElem and Elemental_onBoundary together into a single loop,
// the loop type printed by this function will arbitrarily be one of those
// two.
void
printLoops(std::vector<std::vector<std::vector<Node *>>> loops)
{
  for (size_t i = 0; i < loops.size(); i++)
  {
    auto & loop = loops[i];
    std::cout << "loop " << i + 1 << " (" << loopTypeStr(loop[0][0]->loopType()) << "):\n";
    for (size_t g = 0; g < loop.size(); g++)
    {
      auto & group = loop[g];
      std::cout << "    group " << g + 1 << ": ";
      for (auto n : group)
        std::cout << n->str() << ", ";
      std::cout << "\n";
    }
  }
}

