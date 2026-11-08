// Macro token pasting, default arguments and multi-line actuals.
// Expected values: IEEE 1800-2017 22.5.1 (macro arguments, defaults, ``), 5.7/6.8 (int)
string log = "";
`define dv_error(MSG_, ID_ = "id") log = {log, "E:", MSG_, ":", ID_, ";"};
`define dv_fatal(MSG_, ID_ = "id") log = {log, "F:", MSG_, ":", ID_, ";"};
`define CHK_EQ(ACT_, EXP_, MSG_="", SEV_=error, ID_="i") \
    begin \
      if ((ACT_) == (EXP_)) ; else begin \
        `dv_``SEV_(MSG_, ID_) \
      end \
    end
`define JOIN(A_, B_) A_``_``B_
module svpppaste1;
  int x_y = 5;
  int got;
  initial begin
    `CHK_EQ(1, 2, "x")
    `CHK_EQ(1, 2, "y",
                   fatal, "id")
    `CHK_EQ(1, 2, "z",   fatal  )
    `CHK_EQ(3, 3, "never")
    got = `JOIN(
        x, y);
    if (log != "E:x:i;F:y:id;F:z:i;") $error("log mismatch: %s", log);
    else if (got !== 5) $error("JOIN gave %0d", got);
    else $display("PASSED");
  end
endmodule
