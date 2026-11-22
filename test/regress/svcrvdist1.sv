module svcrvdist1;
  class item_c;
    rand bit [15:0] dur[4];
    rand bit [15:0] key_t;
    rand bit [15:0] cycles;

    constraint dur_c {
      foreach (dur[i]) {
        dur[i] inside {[10:50]};
      }
    }

    constraint key_c {
      key_t inside {[10:50]};
    }

    constraint cycles_c {
      foreach (dur[i]) {
        cycles dist {
          [1 : (dur[i] + key_t) - 2] :/ 20,
          [(dur[i] + key_t) + 5 : (dur[i] + key_t) * 2] :/ 80
        };
      }
    }
  endclass

  initial begin
    automatic item_c it = new();
    for (int t = 0; t < 10; t++) begin
      if (!it.randomize())
        $fatal(1, "randomize failed on iter %0d", t);
      foreach (it.dur[i]) begin
        automatic int s = it.dur[i] + it.key_t;
        automatic bit ok1 = (it.cycles >= 1 && it.cycles <= s - 2);
        automatic bit ok2 = (it.cycles >= s + 5 && it.cycles <= s * 2);
        if (!(ok1 || ok2))
          $fatal(1, "cycles=%0d out of dist ranges for s=%0d", it.cycles, s);
      end
    end
    $display("PASSED");
    $finish;
  end
endmodule
