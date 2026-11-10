// Self-checking: wide (more than 64 bits) rand values in arithmetic, shifts, selects, concatenations,
// reductions, signed relations, solve-before, dist and std::randomize with.
// Expected values: IEEE 1800-2017 18.5 (constraints), 18.5.10 (solve before), 18.5.4 (dist),
// 18.7 (randomize with), 11.4.3 (arithmetic), 11.4.10 (shift), 11.4.9 (reduction), 11.4.12 (concatenation),
// 11.5.1 (selects), 11.3.1 (signed operands).
class Arith;
  rand bit [127:0] a, b;
  rand bit [191:0] sum;
  rand bit [127:0] sh;
  rand bit [127:0] cat;
  rand bit [7:0] sel;
  rand bit signed [99:0] s;
  rand bit [129:0] ones;
  rand bit [9:0] pop;
  rand bit [255:0] big;
  constraint c_add { a == 128'hffff_ffff_ffff_ffff_0000_0000_0000_0001; b == 128'h0000_0000_0000_0001_ffff_ffff_ffff_ffff;
                     sum == 192'(a) + 192'(b); }
  constraint c_sh { sh == 128'h1 << 70; }
  constraint c_cat { cat == {a[63:0], b[127:64]}; }
  constraint c_sel { sel inside {[0:127]}; big[sel] == 1'b1; }
  constraint c_sgn { s < 100'sd0; s > -100'sd5; s != -100'sd3; }
  constraint c_red { ones == (130'h3_ffff_ffff_ffff_ffff_ffff_ffff_ffff_ffff >> 2); pop == $countones(ones); }
  constraint c_big { (big >> 200) == 256'h2a; big[199:0] != 0; big[249:206] == 0; }
endclass

class Order;
  rand bit [95:0] w;
  rand bit [3:0] x;
  constraint c { x inside {[0:3]}; (x == 0) -> (w == 96'h1_0000_0000_0000_0000); (x != 0) -> (w[95:64] == 0); }
  constraint o { solve x before w; }
endclass

class Weighted;
  rand bit [127:0] d;
  constraint c { d dist { 128'h1_0000_0000_0000_0000 := 9, [128'h5 : 128'h6] :/ 1 }; }
endclass

module svcrvwide3;
  Arith k = new;
  Order o = new;
  Weighted wd = new;
  int bad, zero_cnt, nz_cnt, hi_cnt, lo_cnt;
  bit [199:0] w200;
  bit [127:0] sum_or;
  initial begin
    for (int i = 0; i < 100; i++) begin
      if (!k.randomize()) begin $error("Arith randomize failed"); bad++; end
      else begin
        if (k.sum !== 192'(k.a) + 192'(k.b)) begin $error("sum %h", k.sum); bad++; end
        if (k.sh !== 128'h1 << 70) begin $error("sh %h", k.sh); bad++; end
        if (k.cat !== {k.a[63:0], k.b[127:64]}) begin $error("cat %h", k.cat); bad++; end
        if (k.big[k.sel] !== 1'b1) begin $error("sel %0d big %h", k.sel, k.big); bad++; end
        if (!(k.s < 0 && k.s > -5 && k.s != -3)) begin $error("s %0d", k.s); bad++; end
        if (k.ones !== (130'h3_ffff_ffff_ffff_ffff_ffff_ffff_ffff_ffff >> 2) || k.pop != $countones(k.ones)) begin
          $error("ones %h pop %0d", k.ones, k.pop); bad++;
        end
        if ((k.big >> 200) !== 256'h2a || k.big[199:0] == 0 || k.big[249:206] !== 0) begin
          $error("big %h", k.big); bad++;
        end
        w200 |= k.big[199:0];
      end
    end
    if (w200 !== {200{1'b1}}) begin $error("free bits of big not both values: %h", w200); bad++; end

    for (int i = 0; i < 200; i++) begin
      if (!o.randomize()) begin $error("Order randomize failed"); bad++; end
      else begin
        if (o.x == 0 && o.w !== 96'h1_0000_0000_0000_0000) begin $error("x 0 w %h", o.w); bad++; end
        if (o.x != 0 && o.w[95:64] !== 0) begin $error("x %0d w %h", o.x, o.w); bad++; end
        if (o.x == 0) zero_cnt++; else nz_cnt++;
      end
    end
    // solve x before w: x is uniform on 0..3 so x == 0 happens about a quarter of the time
    if (zero_cnt < 20 || zero_cnt > 90) begin $error("x==0 %0d of 200", zero_cnt); bad++; end

    for (int i = 0; i < 400; i++) begin
      if (!wd.randomize()) begin $error("Weighted randomize failed"); bad++; end
      else if (wd.d == 128'h1_0000_0000_0000_0000) hi_cnt++;
      else if (wd.d == 5 || wd.d == 6) lo_cnt++;
      else begin $error("d %h", wd.d); bad++; end
    end
    if (hi_cnt < 300 || lo_cnt < 5) begin $error("dist %0d %0d of 400", hi_cnt, lo_cnt); bad++; end

    begin
      bit [159:0] v;
      bit [7:0] n;
      for (int i = 0; i < 20; i++) begin
        if (!std::randomize(v, n) with { v > 160'h1_0000_0000_0000_0000_0000; v < 160'h1_0000_0000_0000_0000_0100; n == v[3:0]; })
          begin $error("std::randomize failed"); bad++; end
        else if (!(v > 160'h1_0000_0000_0000_0000_0000 && v < 160'h1_0000_0000_0000_0000_0100) || n != v[3:0]) begin
          $error("std v %h n %0d", v, n); bad++;
        end
      end
    end
    if (bad == 0) $display("PASSED");
  end
endmodule
