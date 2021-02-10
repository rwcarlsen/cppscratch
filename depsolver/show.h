#pragma once

#include "graph.h"

#include <string>
#include <vector>
#include <sstream>
#include <iostream>

std::string loopCategoryStr(const LoopCategory cat);

std::string loopTypeStr(const LoopType & l);

std::string nodeLabel(const Subgraph & g, Node * n);

// outputs an a dot script edge pointing from src to dst.  If dst is nullptr,
// then src is created as an "island" node.  If dst is not contained in the
// subgraph g, then the node is filled with an off-yellow color - indicating
// that this node represents a value that is cached and should already be
// available (i.e. computed in a prior loop) - filled nodes are not
// (re)calculated in the current loop, just (re)used.
std::string dotEdge(const Subgraph & g, Node * src, Node * dst);

std::string dotConnections(const Subgraph & g);

// show all the given subgraphs on a single graph.
std::string dotGraphMerged(const std::vector<Subgraph> & graphs);

std::string dotGraph(const Subgraph & g);

// Note that the loop labeling may be partially wrong/incomplete for cases
// where we merged similar loop categories together - e.g. when merging nodes
// from Elemental_onElem and Elemental_onBoundary together into a single loop,
// the loop type printed by this function will arbitrarily be one of those
// two.
void printLoops(std::vector<std::vector<std::vector<Node *>>> loops);

