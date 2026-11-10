# TODO

- always_comb event deferral: an `@(posedge v)` or `@(negedge v)` waiter misses a pulse (0 -> 1 -> 0) that an
  always_comb/always_latch process makes within one activation, because the edge trigger is evaluated against
  the final value. Plain `@(v)` waiters do resume (IEEE 1800-2017 9.4.2). Pinned by test svcombposedge1.
- fork ... join_none: a variable the parent assigns after the fork is not seen by a child that only reads it
  (children share a variable only if some child of the fork assigns it).
