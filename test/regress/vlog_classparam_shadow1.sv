class vcs1_base; static int v = 5; int w = 6; endclass
class vcs1_int extends vcs1_base; int v = 9; endclass
class vcs1_param extends vcs1_base;
  localparam int v = 23;
  parameter int w = 24;
endclass
class vcs1_deeper extends vcs1_param; endclass
module vlog_classparam_shadow1;
  vcs1_int i; vcs1_param p; vcs1_deeper d;
  initial begin
    i = new; p = new; d = new;
    $display("%0d %0d %0d %0d %0d", i.v, p.v, p.w, d.v, vcs1_base::v);
  end
endmodule
