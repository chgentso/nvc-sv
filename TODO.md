# TODO

- always_comb event deferral: an `@(posedge v)` or `@(negedge v)` waiter misses a pulse (0 -> 1 -> 0) that an
  always_comb/always_latch process makes within one activation, because the edge trigger is evaluated against
  the final value. Plain `@(v)` waiters do resume (IEEE 1800-2017 9.4.2). Pinned by test svcombposedge1.
- fork ... join_none: a variable the parent assigns on the same source line as the fork, or in an earlier loop
  iteration before the fork in the text (for example a loop variable the children use directly), is captured by
  value; only assignments in the text after the fork are shared with children that just read the variable.
