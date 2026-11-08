// Self-checking: wide (512-bit) rand variables, bounded queues, rand_mode on an object and on a member.
// Expected values: IEEE 1800-2017 18.5 (constraints), 18.8 (rand_mode: with the object's rand_mode off
// no variable changes; re-enabling one member lets it change), 18.4 (rand variables), 7.10 (queues).
// varies/hi_set are statistical (probability of failure about 2**-300).
class Tx;
  rand bit [511:0] wide;
  rand bit [127:0] w2;
  rand bit [511:0] q[$:8];
  rand logic [95:0] d[];
  rand bit [7:0] n;
  bit [255:0] state;
  constraint c { q.size() == n % 4 + 2; d.size() == 3; n < 100; }
endclass

module svcrvwide1;
  Tx t;
  int ok;
  bit [511:0] prev;
  bit varies, hi_set;
  initial begin
    t = new;
    for (int i = 0; i < 10; i++) begin
      ok = t.randomize();
      if (!ok) $error("randomize failed");
      if (t.q.size() != t.n % 4 + 2 || t.d.size() != 3) $error("bad size");
      if (i > 0 && t.wide != prev) varies = 1;
      if (t.wide[511:480] != 0 || t.q[0][511:256] != 0) hi_set = 1;
      prev = t.wide;
    end
    if (varies !== 1 || hi_set !== 1) $error("varies=%b hi_set=%b, expected 1 1", varies, hi_set);
    t.rand_mode(0);
    prev = t.wide;
    begin
      automatic bit [7:0] n0 = t.n;
      automatic bit [127:0] w20 = t.w2;
      ok = t.randomize();
      if (ok !== 1) $error("randomize with all rand_mode off returned %0d", ok);
      if (t.wide !== prev || t.n !== n0 || t.w2 !== w20) $error("variables changed while rand_mode is off");
      t.wide.rand_mode(1);
      ok = t.randomize();
      if (ok !== 1) $error("randomize with wide enabled returned %0d", ok);
      if (t.wide === prev) $error("wide did not change after rand_mode(1)");
      if (t.n !== n0 || t.w2 !== w20) $error("still-disabled variables changed");
      if (varies === 1 && hi_set === 1 && ok === 1 && t.wide !== prev && t.n === n0 && t.w2 === w20)
        $display("PASSED");
    end
  end
endmodule
