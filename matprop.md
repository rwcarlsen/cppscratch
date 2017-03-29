
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
- [x] handle initial conditions
- [ ] rendezvous/communicate stateful props correctly after mesh refinement/repartitioning

Redesign notes:

* Current stateful property support only includes old values - it can be trivially extended to
  older values with the addition of corresponding [bla]Older functions and a few mods to the shift
  function.
* Code is very simple and very short.
* Calculating/retrieving many properties is fast
* Preserves not storing/cacheing/reusing material properties between computations unless needed
  for stateful properties.
* Full material dependency tracking is in force - only compute materials that
  are needed.  This is automagic with no complicated code or user input
  required.
 
