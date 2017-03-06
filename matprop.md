
* Material (props) must be block/boundary restrictable
* Must handle mesh refinement/projection
* handle restart/recovery
* handle initial conditions
* [pseudo-done] handle stateful materials

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

