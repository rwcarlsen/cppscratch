
Progress reimplementing current material property system features:

- [x] support properties of arbitrary c++ type
- [x] handle block restrictability
- [x] handle mesh refinement/projection
- [x] Handle stateful materials
- [x] Error on cyclical dependencies
- [x] Error on wrong cpptype prop id requests.
- [ ] handle restart/recovery

Progress implementing new features:

- [x] full, fine-grained property dependency resolution
- [ ] rendezvous stateful props correctly after mesh refinement/repartitioning
- [ ] handle initial conditions

Redesign notes:

* Code is very simple and very short.
* Calculating/retrieving many properties is fast
* Preserves not storing/cacheing/reusing material properties between computations unless needed
  for stateful properties.
* Full material dependency tracking is in force - only compute materials that
  are needed.  This is automagic with no complicated code or user input
  required.
 
