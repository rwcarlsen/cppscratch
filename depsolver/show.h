#pragma once

#include "graph.h"

#include <string>
#include <vector>
#include <sstream>
#include <iostream>

std::string
loopTypeStr(const LoopType & l)
{
  std::string s = "Loop:";
  if (l.category == LoopCategory::None)
    s += "None";
  else if (l.category == LoopCategory::Nodal)
    s += "Nodal";
  else if (l.category == LoopCategory::Face)
    s += "Face";
  else if (l.category == LoopCategory::Elemental_onElem)
    s += "Elemental_onElem";
  else if (l.category == LoopCategory::Elemental_onElemFV)
    s += "Elemental_onElemFV";
  else if (l.category == LoopCategory::Elemental_onBoundary)
    s += "Elemental_onBoundary";
  else if (l.category == LoopCategory::Elemental_onInternalSide)
    s += "Elemental_onInternalSide";
  else
    s += "UNKNOWN";
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
  for (auto & g : graphs)
    ss << dotConnections(g);
  ss << "}\n";
  return ss.str();
}

std::string
dotGraph(const Subgraph & g)
{
  std::stringstream ss;
  ss << "digraph g {\n";
  dotConnections(g);
  ss << "}\n";
  return ss.str();
}

void
printLoops(std::vector<std::vector<std::vector<Node *>>> loops)
{
  for (size_t i = 0; i < loops.size(); i++)
  {
    auto & loop = loops[i];
    std::cout << "loop " << i + 1 << ":\n";
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

