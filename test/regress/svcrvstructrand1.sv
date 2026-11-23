// A rand variable of unpacked structure type named in the argument list of
// randomize solves its members which are declared rand, also nested ones
typedef struct { rand bit [1:0] op; rand bit [31:0] words; rand bit [31:0] addr; } fop_t;
typedef struct { rand bit [3:0] r; bit [3:0] fixed; } mixed_t;

class seq;
  rand fop_t f;
  rand mixed_t m;
  constraint c {
    f.op inside {0, 1, 2};
    f.addr inside {[0:65535]};
  }
  function new(); m.fixed = 4'hA; endfunction
  function bit run(bit args);
    return args ? randomize(f) : randomize();
  endfunction
endclass

module svcrvstructrand1;
  seq s;
  int bad, w_diff, r_diff;
  initial begin
    s = new;
    for (int n = 0; n < 20; n++) begin
      if (!s.run(n % 2)) bad++;
      if (s.f.op > 2 || s.f.addr > 65535) bad++;
      if (s.f.words != 0) w_diff++;
      if (s.m.fixed != 4'hA) bad++;
      if (s.m.r != 0) r_diff++;
      s.m.r = 0;
    end
    if (bad == 0 && w_diff > 10 && r_diff > 3) $display("PASSED");
    else $display("FAILED %0d %0d %0d", bad, w_diff, r_diff);
  end
endmodule
