# TODO

- always_comb event deferral: an `@(posedge v)` or `@(negedge v)` waiter misses a pulse (0 -> 1 -> 0) that an
  always_comb/always_latch process makes within one activation, because the edge trigger is evaluated against
  the final value. Plain `@(v)` waiters do resume (IEEE 1800-2017 9.4.2). Pinned by test svcombposedge1.
- fork ... join_none: a variable the parent assigns on the same source line as the fork, or in an earlier loop
  iteration before the fork in the text (for example a loop variable the children use directly), is captured by
  value; only assignments in the text after the fork are shared with children that just read the variable.
- fork ... join_none capture is a heuristic, not the shared-lifetime model of IEEE 1800-2017 6.21/9.3.2 (an
  automatic variable referenced by a forked process is shared and lives while referenced).  A subroutine local
  is shared with the children only if some fork in the subroutine assigns it (by name, element, slice or
  field) or the subroutine assigns it in the text after the fork; otherwise it is copied at spawn.  Still
  wrong: a variable the parent assigns after the fork but outside the text order (loop back-edge), one written
  through a task output/ref argument or by a method call (queue push_back, associative delete) and read by the
  forked process, and forks directly in an initial/always block (no enclosing subroutine to scan), where a
  variable written by a sibling fork is not shared.
- Variable output port connected to a net (IEEE 1800-2017 23.3.3: a continuous driver of the net): the port is
  aliased to the net unless the parent has more than one assign target, gate or instance connection naming
  that net.  A procedural write to the aliased variable is a deposit, so it is still lost when the net is
  resolved again if the other driver is not visible in the parent text: a hierarchical assignment from
  elsewhere (`top.w = ...` via assign), a force, or a tran/inout path.
- A class may have more than 64 rand properties, but only the first 64 have a rand_mode bit: the others are
  always random and rand_mode() on one of them is rejected at lowering ("sorry, rand_mode of the property ...").
  The bits are held in one 64-bit hidden property; a wider mask needs more than one word.
- $fatal (and a failed constant function) inside a function evaluated while elaborating, such as the value of a
  parameter, is shown as a warning and evaluation carries on; 1800-2017 13.4.3 does not allow system tasks in a
  constant function so a conforming program does not rely on either behaviour.  OpenTitan's lc_ctrl_dv_utils_pkg
  calls $fatal on a path its own parameter initialisers take.
- A bind directive is resolved as if written in the target module, but only for the names that module declares:
  a name the target gets from a wildcard package import (for example `bind alert_handler ... #(.N(NLpg))`
  with NLpg from `import alert_handler_reg_pkg::*` in the module header) is not found.
- The key of a class specialisation is made from the folded constant value of the parameters.  An expression of
  other parameters is evaluated by spec_eval (+ - * /), a parameter given by anything else (function call,
  conditional, shift) still gets a key that depends on where it is written.
- vlog_get_const / vlog_is_const evaluate an element of a parameter array (positional '{...} patterns of one
  dimension) and a member of a structure in one (named or positional); keyed '{0: ..}' items, default items
  and multidimensional arrays are not evaluated, so an index taken from them is not a static prefix.
- A constraint can look up a member of an element of a parameter array of packed structures with a random
  index; the table is passed to the solver as an array each time randomize() is called.  Unpacked structures
  in a parameter array have not been tried with a random index.
