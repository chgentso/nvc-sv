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
