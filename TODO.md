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
- A foreach constraint over a packed array of packed structures is unrolled while the constraint is built and a
  member of an element is a slice of the array (vlog_crv_slice_node).  An equality on a slice of a variable wider
  than 64 bits is only reliably solved when the slice lies inside one 64-bit chunk of the variable: a slice that
  crosses a chunk boundary is an expression of two solver variables and randomize() often fails on it (about 8 in
  10 attempts for `rand bit [107:0] v; v[71:54] == 18'h1234`).  It would need the equality split per chunk.
- A constraint on a very wide variable with several parts, for example OpenTitan's mem_bkdr_util.inject_errors on
  `logic [1023:0]` (`$countones(m) == n; (m >> width) == '0;`), often fails (randomize returns 0 for roughly two
  calls in three).  The pieces solve on their own; the shift by a state variable builds a barrel shifter of
  stages whose conditions are constants, folding those in crv_cond helps only a little.
- Signed constants and unsigned operands (IEEE 1800-2017 11.8.2): a comparison with a constant such as `t <= -1` where t is an
  unsigned 64 bit variable is true although -1 is a 32 bit signed value that is converted to unsigned before it is extended,
  and `u inside {s}` with a signed 32 bit s compares the sign extended value; the same comparison of two variables is
  right.  The dist of a constraint is converted correctly since vlog_crv_dist_bound, the other constraint operators
  are not checked for this.
- A for-generate loop is compiled once for all iterations only when a whitelist allows it
  (`NVC_GEN_SHARE_VERBOSE=1` prints the reason for each loop that is not): loops with assertions, sampled-value
  functions, subprograms, classes, hierarchical names, defparams, coverage enabled, or the genvar in a width,
  dimension, part-select bound, replication count, cast, instance parameter, instance array, inout port or
  event control keep one body for each iteration and the memory of that.  Each iteration still has its own
  scope and block function, about 7-11 kB at run time.  A parameter of a shared iteration read through a
  hierarchical name is evaluated from the genvar values only for arithmetic, comparison and conditional
  expressions; anything else is a "sorry" (NVC_GEN_SHARE=off avoids it).
- Constrained random, limits found by the review of the solver (own tests in test/regress/svcrv*; none of these
  is a crash, each is a sorry, a warning or a randomize() that fails):
  - `randsequence` (IEEE 1800-2017 18.17): production weights, `rand join`, `break`/`return` inside a production
    and arguments of productions are not all supported or tested; a sequence that uses one of them may take the
    wrong branch.  Not looked at in this review.
  - `rand real` and a real value in a constraint (1800-2023): not supported; the property is not randomized.
  - `**` in a constraint: a constant exponent is expanded to multiplications (also a negative one), the power
    of two with a variable exponent is a shift; any other variable exponent, and `**` on a value wider than 64
    bits, is a sorry.
  - A function called in a constraint with a random variable as an argument is a run-time sorry that names the
    function: IEEE 1800-2017 18.5.12 solves the argument first and calls the function, which needs a staged
    solve (the function result then takes part in the constraints).
  - A constraint over the elements of a dynamic array whose size the solver chooses, when it needs a call or
    a dist per element (the foreach is unrolled over the length the array has when randomize() is called), is
    built for that length: the first call on an empty array gets no element constraints.  `sum() with (e)`,
    `product()`, `and()`, `or()` and `xor()` with an expression over such an array are built for the current
    length and add the constraint size == length, so randomize() fails (with the unchecked-result warning)
    until the array has been given the size the constraints ask for.  `sum() with (item)` and `sum() with
    (int'(item))` are exact for any size.
  - An element of a dynamic array or queue of values wider than 64 bits in a constraint, and a constraint on a
    queue of queues or a dynamic array of dynamic arrays that is not a rand property, are a sorry or a warning.
  - `static` rand variables are not randomized (the property keeps its value); the mode of a static constraint
    is shared by all objects but rand_mode() of a static rand variable is per object.
  - randomize(obj) with an object handle, and randomize(a.b) with a member of another object, as arguments are a
    sorry; `with (list)` and local:: bind names as the standard says but a local:: function call is made on the
    object that called randomize() only when the function is not also a method of the randomized class.
  - Out of range index of an element in a foreach constraint (`a[i] == a[i-1]` for i == 0) makes the
    constraint, and so randomize(), fail instead of leaving the element out.
