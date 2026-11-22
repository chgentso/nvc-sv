// A constraint on an element of a fixed size array of queues is not held
// by the solver: a clean error, not a crash
class c;
  rand int pages[2][$];
  constraint s { foreach (pages[i]) pages[i].size() == i + 1; }
endclass

module svcrvnest1;
  c o;
  initial begin
    o = new;
    void'(o.randomize());
  end
endmodule
