// A foreach constraint over a packed array of packed structures visits the
// elements of the array, from the left bound to the right bound, and a member
// of an element can be constrained (IEEE 1800-2017 18.5.8).
module svrandforeachpacked1;
  typedef struct packed { bit en; bit inv; bit [15:0] delay; } preg_t;

  class c;
    rand preg_t [5:0] p;
    constraint k_all {
      foreach (p[i]) {
        p[i].en == 1'b1;
        p[i].inv == (i % 2 == 1);
      }
    }
    constraint k_delay {
      p[4].delay == 16'h1234;
      p[1].delay == 16'h0001;
      p[0].delay == 16'hbeef;
    }
  endclass

  initial begin
    automatic c o = new;
    automatic int errs = 0;
    for (int n = 0; n < 10; n++) begin
      if (!o.randomize()) begin
        errs++;
        $error("randomize failed");
      end
      else begin
        for (int i = 0; i < 6; i++)
          if (o.p[i].en !== 1'b1 || o.p[i].inv !== (i % 2 == 1)) begin
            errs++;
            $error("element %0d has en=%b inv=%b", i, o.p[i].en, o.p[i].inv);
          end
        if (o.p[4].delay !== 16'h1234 || o.p[1].delay !== 16'h0001 || o.p[0].delay !== 16'hbeef) begin
          errs++;
          $error("delays %h %h %h", o.p[4].delay, o.p[1].delay, o.p[0].delay);
        end
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
