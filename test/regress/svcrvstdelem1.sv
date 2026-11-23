// An element of an array which is an argument of std::randomize is solved with the constraints that name
// it: for an array of the module, a property of an object, and two dimensions (IEEE 1800-2017 18.12)
class T;
  logic [3:0] member[5];
  logic [3:0] m2[3][4];
endclass
module svcrvstdelem1;
  bit [3:0] w[5];
  bit [7:0] cube[4][2];
  T e;
  int ok, fails = 0;
  initial begin
    e = new;
    repeat (30) begin
      ok = std::randomize(w[1]) with { w[1] inside {[0:3]}; };
      if (!ok || w[1] > 3) begin fails++; $error("w[1]=%0d", w[1]); end
      ok = std::randomize(e.member[2]) with { e.member[2] inside {[5:7]}; };
      if (!ok || e.member[2] < 5 || e.member[2] > 7) begin fails++; $error("member[2]=%0d", e.member[2]); end
      ok = std::randomize(e.m2[1][2]) with { e.m2[1][2] inside {[8:12]}; };
      if (!ok || e.m2[1][2] < 8 || e.m2[1][2] > 12) begin fails++; $error("m2[1][2]=%0d", e.m2[1][2]); end
      ok = std::randomize(cube[1][0], cube[2][1]) with { cube[1][0] == 5; cube[2][1] == 9; };
      if (!ok || cube[1][0] != 5 || cube[2][1] != 9) begin fails++; $error("cube"); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
