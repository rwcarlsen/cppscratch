

MOOSE Dependency Graph Description
===================================

FEM calculations in MOOSE involve calculating several quantities at specific
sets of mesh locations.  These quantities include material properties, PDE
weak form residual terms, etc.  Sets of locations include integration
quadrature points within every mesh element, at all mesh nodes, at all faces
adjoining neighboring elements, etc.  Because many of these quantities can
depend on each other, they must be calculated in a particular order.  Some
quantities are reducing/aggregation values that require calculating and
looping over the entire mesh.  Values that depend on such quantities must run
later in separate loops over the mesh.  We want to try to minimize the number
of mesh loops required to calculate all desired quantities.  We also want to
allow flexible mesh loop operations - to allow for arbitrarily complex or
simple calculations involving any combination of dependencies between
quantities of interest.

Graph Partitioning Algorithm
=============================

Each quantitity is a node in our graph.  Directed edges define dependencies
between these quantities.  Using information about each node, we must
determine subsets of nodes that can run together in a single "loop" over the
mesh.  Node properties that affect this partitioning operation are:

* Location: This describes where on the mesh the quantity is calculated (e.g. nodes,
  faces, element quadrature points, etc.).  This also includes which
  subdomain/block the quantity may have been restricted to.  MOOSE allows
  something called "block restriction" where certain quantities are only
  computed in certain subdomains of the mesh.  In effect each subdomain can
  it's own unique subgraph of dependencies.

* Persistence: This indicates whether a quantity is cached (stored/saved)
  after being calculated or whether it is ephemeral and needs to be
  recalculated on-demand when needed.

* Availability: This effectively indicates whether or not the process of
  computing a quantity is a "reducing" or aggregation operation and the
  quantity will not be available until _after_ an entire mesh loop operation
  has completed.


Basic Splitting
-------------------

The basic partitioning process can be described as follows:

1. Cut the graph at every edge between nodes where the Location property
   differs.


2. Cut the graph at every edge between nodes where the dependency (the
   depended on node or one the edge points toward) is a reducing/aggregation
   node.

3. Each unconnected subgraph becomes its own partition or mesh loop.

4. For each subtraph/loop recursively duplicate any dependency nodes that are
   uncached/ephemeral - stopping the recucursion when hitting uncached nodes.
   This step is necessary because our original graph only had one node for each
   ephemeral quantity, but each mesh loop that needs a particular ephemeral
   quantity will need to include/recalculate it directly.


Fully Merging Partitions
-------------------------

After the basic splitting process above it is possible (and indeed not
unlikely) that the set of partitions or mesh loops can be further consolidated
into fewer partitions.  There are two conditions determining mergeability of
these partitions - both must be met:

1. The partitions must have the same mesh Location property.

2. Partition A must not depend on any cached node (including transitively) in
   Partition B.  Partition B must not depend on any cached node (including
   transitively) in Partition A.  In a sense, the partitions must be siblings -
   neither can be a parent of the other.

A "fully merged" set of partitions is one for which it is impossible to merge
any two partitions.  There is no unique fully merged solution to this problem.
An example below shows two unique fully merged versions of a single, starting
set of partitions:

TODO: insert graphic here

I have implemented a heuristic/algorithm that tries to generated a "good"
(i.e. as few partitions as possible post merging) fully merged partition set.
It is guaranteed to generate a fully merged partition set.  Solving this
problem, it makes sense to create a meta graph where each node represents a
partition of our full dependency graph (i.e. each node represents the group of
nodes in each of our partitions).  After performing the merging process on
this meta graph, we then map those mergers back into our original full graph
partitions where each node represented a calculation/quantity of interest.

The algorithm does the following:

1. Make sure each partition represents one (and only 1) connected set of
   nodes.  This may require splitting partitions that contain collections of
   unconnected nodes.

2. Create the meta graph where a node represents a loop/partition. Edges
   represent dependencies between partitions (i.e. nodes of each partition).

3. Calculate every possible candidate pair of partition (i.e. meta node) merges.

4. For each pair of candidate merges, calculate which of the other candidate
   merges are incompatible/prevented by a given merge.  Since merging two
   partitions causes the new merged partition to have the union of dependencies
   and union of dependers of both prior partitions, this affects which other
   merges are now possible.  This is the most expensive part of the algorithm.

5. Sort the candidate merges ascending based on how many other merges they "cancel".

6. Loop over the sorted candidate merges selecting each merger one at a time -
   and eliminating the ones it cancels.  As this loop continues, skip
   over canceled merges.  Continue this process until you reach the end of
   the candidate merges list.

7. The selected merges are then mapped back to the non-meta/original graph
   partitions and performed.

