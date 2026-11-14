module mixedparam1v_m #(parameter bit g_sim = 0, parameter logic g_l = 1'b0,
                        parameter int g_n = 3) (output logic [7:0] o);
  assign o = {4'(g_n), 1'(g_l === 1'bz), g_l === 1'b1, g_sim, 1'b0};
endmodule
