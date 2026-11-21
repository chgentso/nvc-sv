function void f_ev;
endfunction

task t_var;
endtask

module callorder1;
  initial begin
    f_ev();
    t_var();
  end

  event f_ev;
  integer t_var;
endmodule
