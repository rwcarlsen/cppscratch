
Progress reimplementing current material property system features:

- [x] support properties of arbitrary c++ type
- [x] handle block restrictability
- [x] handle mesh refinement/projection
- [x] Handle stateful materials
- [x] Error on cyclical dependencies
- [x] Error on wrong cpptype prop id requests.
- [x] handle restart/recovery

Progress implementing new features:

- [x] full, fine-grained property dependency resolution
- [x] handle initial conditions
- [ ] rendezvous/communicate stateful props correctly after mesh refinement/repartitioning

Weaknesses:

* Property value retrieval becomes a function call instead of a member access - a bit more
  verbose - i.e. `return _myprop[_qp]` becomes `return prop<Real>("myprop", loc)`;

Strengths:

* Preserves not storing/cacheing/reusing material properties between computations unless needed
  for stateful properties.

* Supports initial conditions for both old and older values.
 
* No longer necessary to block/boundary restrict for performance reasons. Material properties will
  automatically only be evaluated at mesh locations where they are needed.

* Natural property mapping by mesh location (not just by block) is trivial/easy and requires no
  special implementation changes/code for material objects.  You can just:

  ```
  [p1]
    type=AProp
    prop=subprop1
    ...
  [../]
  [p2]
    type=AnotherProp
    prop=subprop2
    ...
  [../]

  [map]
    type=BlockMap
    prop='my-master-prop'
    # pattern is subprops='[subprop1] [block-id]..., [subprop2] [block-id]..., etc'
    subprops='subprop1 1 2 3, subprop2 4 5, subprop3 6 7 8 9'
  [../]
  ```

  noting that ``AProp`` and ``AnotherProp`` don't need any handling of block id or any other logic
  for this to work.

* No complicated memory swapping/tracking with sentinels, etc.  No scattered memory locations
  across Material object instances reserved to store/swap in computed values (less memory, less
  confusing).

* Fine-grained property dependency tracking works implicitly and naturally via the function call
  trees for fetching property values.  Property values are never calculated where/when they aren't
  needed.

* Allows users to store stateful values on the mesh at their own custom locations. They just
  create a custom comparator and ``ValueStore<CustomCmp> _custom_store;`` wherever they need.

* Users don't need to worry about quadrature point indexing on property values.

* Whether or not displaced mesh locations should be used comes in on the passed in Location
  objects.

* Plenty fast enough - can retrieve a few billion (constant) properties per minute on my machine
  in serial

* Code is relatively simple and relatively short w.r.t. the current/legacy system.

