// Self-checking: constraints on rand values wider than 64 bits: equality, inequality, inside with wide
// constants, wide ranges, wide and narrow mixes and a 256-bit key with a mask.
// Expected values: IEEE 1800-2017 18.3 and 18.5 (constraints apply to integral types of any width),
// 18.5.3 (inside), 11.4.5 (equality), 11.4.4 (relational), 11.4.8 (bitwise).
// The distribution checks are statistical.  The solutions are drawn uniformly, so the sets of iv are
// small enough for each to be seen in 200 draws (probability of failure below 1e-12).
class Key;
  rand bit [255:0] key;
  rand bit [127:0] iv;
  rand bit [199:0] odd;
  rand bit [7:0] n;
  rand bit [95:0] low;
  rand bit [127:0] free_bits;
  constraint c_key { (key & 256'hffff_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_ffff)
                       == 256'h1234_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_5678;
                     key != 256'h0; }
  constraint c_iv { iv inside {128'h1, 128'h2,
                               [128'h1_0000_0000_0000_0000 : 128'h1_0000_0000_0000_0003],
                               128'hffff_ffff_ffff_ffff_ffff_ffff_ffff_ff00}; }
  constraint c_odd { odd >= 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000;
                     odd < 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0010;
                     odd != 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0005; }
  constraint c_mix { n inside {[1:4]}; low == {88'h0, n}; low[95:8] == 0; }
endclass

module svcrvwide2;
  Key k = new;
  int bad;
  bit [255:0] seen_or, seen_and;
  bit [127:0] fr_or, fr_and;
  int n_iv_small, n_iv_range, n_iv_big;
  initial begin
    seen_and = '1; fr_and = '1;
    for (int i = 0; i < 200; i++) begin
      if (!k.randomize()) begin $error("randomize failed"); bad++; end
      else begin
        if ((k.key & 256'hffff_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_ffff)
              !== 256'h1234_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_5678
            || k.key == 0) begin $error("key %h", k.key); bad++; end
        if (k.iv == 1 || k.iv == 2) n_iv_small++;
        else if (k.iv >= 128'h1_0000_0000_0000_0000 && k.iv <= 128'h1_0000_0000_0000_0003) n_iv_range++;
        else if (k.iv == 128'hffff_ffff_ffff_ffff_ffff_ffff_ffff_ff00) n_iv_big++;
        else begin $error("iv %h", k.iv); bad++; end
        if (k.odd < 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000
            || k.odd >= 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0010
            || k.odd == 200'h1_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0005) begin
          $error("odd %h", k.odd); bad++;
        end
        if (!(k.n inside {[1:4]}) || k.low != k.n) begin $error("n %0d low %h", k.n, k.low); bad++; end
        seen_or |= k.key; seen_and &= k.key;
        fr_or |= k.free_bits; fr_and &= k.free_bits;
      end
    end
    // Bits the constraints leave free take both values
    if (((seen_or | 256'hffff_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_ffff) != '1)
        || ((seen_and & ~256'hffff_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_0000_ffff) != 0)) begin
      $error("key bits not free: or %h and %h", seen_or, seen_and); bad++;
    end
    if (fr_or != '1 || fr_and != 0) begin $error("free bits or %h and %h", fr_or, fr_and); bad++; end
    if (n_iv_small == 0 || n_iv_range == 0 || n_iv_big == 0) begin
      $error("iv sets %0d %0d %0d", n_iv_small, n_iv_range, n_iv_big); bad++;
    end
    if (bad == 0) $display("PASSED");
  end
endmodule
