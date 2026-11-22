package svforeachvif1_pkg;
  typedef enum logic [1:0] {
    ST_A = 2'd0,
    ST_B = 2'd1
  } state_e;

  typedef virtual svforeachvif1_if vif_t;

  class checker_c;
    vif_t vif;

    function int sum_all();
      int total = 0;
      foreach (vif.keys_a_array[st, dest]) begin
        total += vif.keys_a_array[st][dest];
        if (dest == "beta" && st == ST_A)
          total += 100;
      end
      return total;
    endfunction

    function int sum_dyn();
      int total = 0;
      foreach (vif.dyn_arr[i]) begin
        total += vif.dyn_arr[i];
      end
      return total;
    endfunction

    function int sum_fixed();
      int total = 0;
      foreach (vif.fixed_arr[i]) begin
        if (i == 1) continue;
        total += vif.fixed_arr[i];
      end
      return total;
    endfunction
  endclass
endpackage

interface svforeachvif1_if;
  import svforeachvif1_pkg::*;
  int keys_a_array[state_e][string];
  int dyn_arr[];
  int fixed_arr[0:2];
endinterface

module svforeachvif1;
  import svforeachvif1_pkg::*;

  svforeachvif1_if u_if();

  initial begin
    automatic checker_c chk = new();
    int r1, r2, r3;
    chk.vif = u_if;

    u_if.keys_a_array[ST_A]["alpha"] = 10;
    u_if.keys_a_array[ST_A]["beta"]  = 20;
    u_if.keys_a_array[ST_B]["gamma"] = 30;

    u_if.dyn_arr = new[3];
    u_if.dyn_arr[0] = 3;
    u_if.dyn_arr[1] = 5;
    u_if.dyn_arr[2] = 7;

    u_if.fixed_arr[0] = 11;
    u_if.fixed_arr[1] = 22;
    u_if.fixed_arr[2] = 33;

    r1 = chk.sum_all();
    r2 = chk.sum_dyn();
    r3 = chk.sum_fixed();

    if (r1 !== 160) $fatal(1, "r1=%0d exp=160", r1);
    if (r2 !== 15)  $fatal(1, "r2=%0d exp=15", r2);
    if (r3 !== 44)  $fatal(1, "r3=%0d exp=44", r3);
    $display("PASSED");
    $finish;
  end
endmodule
