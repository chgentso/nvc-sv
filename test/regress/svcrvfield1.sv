// Self-checking: every randomize() must succeed and satisfy the field constraints.
// Expected values: IEEE 1800-2017 18.5 (constraint blocks, part-select and concatenation
// constraints), 18.5.4 (dist weights), 11.5.1 (part-selects).
module svcrvfield1;
  class c;
    rand bit [31:0] w;
    rand bit [63:0] q;
    rand bit [31:0] v;
    rand bit [20:0] y;
    rand int unsigned s;
    rand bit [3:0] a, b;
    rand bit [6:0] lo;
    constraint cw {
      w[6:0] == lo;
      w[11:8] == a;
      w[19:16] == b;
      w[31:25] inside {7'd1, 7'd2, 7'd3};
      w[23:20] != 0;
      w[15:12] > a;
      w[3:2] == 2'b11;
      w != 32'h0;
      w < 32'hf0000000;
    }
    constraint cq {
      q[63:48] == 16'hbeef;
      q[7:0] != 8'h00;
      q[15:8] inside {[8'h10:8'h1f]};
      {q[22], q[30:28]} == 4'b1010;
    }
    constraint cv {
      v inside {[100:200000]};
      v[3:0] == 5;
      v[5:4] != 2'b00;
    }
    constraint cy {
      y dist {[0:99] := 1, [100:1000] :/ 4};
      y[2:0] == 3'b011;
    }
    constraint cs {
      s[31:28] == 4'hc;
      s[3:0] < 4'd5;
      s > 32'hc0000000;
    }
  endclass

  initial begin
    automatic c o = new;
    automatic int bad = 0, viol = 0;
    automatic bit [2:0] seen = 0;
    automatic int ylow = 0, yhigh = 0;
    repeat (2000) begin
      if (!o.randomize()) bad++;
      else begin
        if (o.w[6:0] != o.lo || o.w[11:8] != o.a || o.w[19:16] != o.b
            || !(o.w[31:25] inside {1, 2, 3}) || o.w[23:20] == 0
            || o.w[15:12] <= o.a || o.w[3:2] != 3 || o.w == 0
            || o.w >= 32'hf0000000) viol++;
        if (o.q[63:48] != 16'hbeef || o.q[7:0] == 0
            || !(o.q[15:8] inside {[8'h10:8'h1f]})
            || {o.q[22], o.q[30:28]} != 4'b1010) viol++;
        if (!(o.v inside {[100:200000]}) || o.v[3:0] != 5 || o.v[5:4] == 0)
          viol++;
        if (o.y[2:0] != 3 || o.y > 1000) viol++;
        if (o.s[31:28] != 4'hc || o.s[3:0] >= 5 || o.s <= 32'hc0000000)
          viol++;
        seen[o.w[31:25] - 1] = 1;
        if (o.y < 100) ylow++; else yhigh++;
      end
    end
    if (bad != 0) $error("randomize failed %0d times", bad);
    if (viol != 0) $error("%0d constraint violations", viol);
    // statistical: all three legal w[31:25] values appear in 2000 draws
    if (seen !== 3'b111) $error("seen=%b, expected all of 1,2,3", seen);
    // dist weights: ~13 low-side weight against ~0.5 high-side weight
    if (!(ylow > yhigh)) $error("y distribution not skewed low: low=%0d high=%0d", ylow, yhigh);
    if (!bad && !viol && seen === 3'b111 && ylow > yhigh) $display("PASSED");
  end
endmodule
