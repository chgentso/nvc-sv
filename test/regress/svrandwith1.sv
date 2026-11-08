class a;
  rand int x;
  rand int z;
  int y = -1;
endclass

function int F(a obj, int y);
  F = obj.randomize() with (x) { x > 0; x < y; };
endfunction

function int G(a obj, int y);
  G = obj.randomize() with { x > 0; x < 5; };
endfunction

function int H(a obj, int z);
  H = obj.randomize() with (x, z) { x > 0; x < 5; z == 7; };
endfunction

function int I(a obj, int z);
  I = obj.randomize() with (x) { x > 0; x < 5; x != z; };
endfunction

module svrandwith1;
  a obj = new;
  int r;
  initial begin
    repeat (20) begin
      r = F(obj, 10);
      if (r != 1 || obj.x < 1 || obj.x > 9) $fatal(1, "F %0d %0d", r, obj.x);
      r = G(obj, 10);
      if (r != 1 || obj.x < 1 || obj.x > 4) $fatal(1, "G %0d %0d", r, obj.x);
      r = H(obj, 3);
      if (r != 1 || obj.x < 1 || obj.x > 4 || obj.z != 7)
        $fatal(1, "H %0d %0d %0d", r, obj.x, obj.z);
      r = I(obj, 2);
      if (r != 1 || obj.x < 1 || obj.x > 4 || obj.x == 2)
        $fatal(1, "I %0d %0d", r, obj.x);
    end
    $display("PASSED");
  end
endmodule
