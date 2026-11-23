// A foreach constraint over a fixed size array of values wider than 64 bits
class c;
  rand bit [94:0] a[4];
  constraint k { foreach (a[i]) a[i] < 95'd1000000000000 + i; }
endclass
module svcrvwideforeach1;
  c o; int bad;
  initial begin
    o = new;
    repeat (5) begin
      if (!o.randomize()) bad++;
      foreach (o.a[i]) if (o.a[i] >= 95'd1000000000000 + i) bad++;
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED %0d", bad);
  end
endmodule
