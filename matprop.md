
* Material (props) must be block/boundary restrictable.  This is automagically
  handled by using the QpValuer inner class and implicit/direct dependency
  resolution.  Materials will just never be called for areas where they are not needed.

* Must handle mesh refinement/projection
* handle restart/recovery
* handle initial conditions
* Handle stateful materials. Done.
* Error on cyclical dependencies
* Error on wrong cpptype prop id requests.

current redesign notes:

* Code is very simple and very short.

* Calculating/retrieving many properties is fast

* Preserves not storing/cacheing/reusing material properties between computations

* Full material dependency tracking is in force - only compute materials that
  are needed.  This is automagic with no complicated code or user input
  required.
 
* Handles statefulness a bit rough - but it is explicit and more powerful and
  fairly straight forward. Could potentially use some polish

* A single stateful property used by multiple sources is stored multiple
  times.  Not sure how important it is to not do this.  We could change it.

Abstract Requirements:

* Calculate+store+retrieve arbitrary c++ typed data at each quadrature point on mesh
* Must be fast
* Want explicit calculation upon request of datum

