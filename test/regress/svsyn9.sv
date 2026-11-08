// Register model with callbacks, queues of objects and parameterised fields
// Expected values: IEEE 1800-2017 8.15/8.21 (virtual methods, abstract classes), 8.25 (parameterized classes),
// 7.8 (associative arrays), 7.10 (queues), 11.4.10 (shifts), 21.2.1 (%h)
string mask_log[$];
int nosuch;
typedef class reg_block;
virtual class reg_cb;
  pure virtual function void post_write(string name, int unsigned old_v, int unsigned new_v);
endclass
class log_cb extends reg_cb;
  int count;
  string last;
  virtual function void post_write(string name, int unsigned old_v, int unsigned new_v);
    count++; last = $sformatf("%s:%0h->%0h", name, old_v, new_v);
  endfunction
endclass
class mask_cb extends reg_cb;
  int unsigned mask;
  function new(int unsigned m); mask = m; endfunction
  virtual function void post_write(string name, int unsigned old_v, int unsigned new_v);
    if ((old_v ^ new_v) & mask) mask_log.push_back($sformatf("%s changed masked bits %08h", name, (old_v ^ new_v) & mask));
  endfunction
endclass
class reg_field #(int W = 8);
  string name; bit [W-1:0] value; bit [W-1:0] reset_v; int lsb;
  function new(string n, int l, bit [W-1:0] r = '0); name = n; lsb = l; reset_v = r; value = r; endfunction
  function bit [31:0] get_mask(); return ((1 << W) - 1) << lsb; endfunction
endclass
class my_reg;
  string name; int unsigned value; reg_cb cbs[$]; reg_field#(4) f4[$]; reg_field#(8) f8[$];
  function new(string n); name = n; endfunction
  function void add4(reg_field#(4) f); f4.push_back(f); endfunction
  function void add8(reg_field#(8) f); f8.push_back(f); endfunction
  function void write(int unsigned v);
    int unsigned old_v = value;
    value = v;
    foreach (f4[i]) f4[i].value = (v >> f4[i].lsb) & 4'hf;
    foreach (f8[i]) f8[i].value = (v >> f8[i].lsb) & 8'hff;
    foreach (cbs[i]) cbs[i].post_write(name, old_v, v);
  endfunction
  function int unsigned read(); return value; endfunction
  function string describe();
    string s = name;
    foreach (f4[i]) s = {s, $sformatf(" %s=%0h", f4[i].name, f4[i].value)};
    foreach (f8[i]) s = {s, $sformatf(" %s=%0h", f8[i].name, f8[i].value)};
    return s;
  endfunction
endclass
class reg_block;
  my_reg regs[string];
  my_reg order[$];
  function void add(my_reg r); regs[r.name] = r; order.push_back(r); endfunction
  function void write(string n, int unsigned v);
    if (!regs.exists(n)) begin nosuch++; return; end
    regs[n].write(v);
  endfunction
endclass
module svsyn9;
  reg_block blk;
  my_reg ctrl, status;
  log_cb lcb;
  mask_cb mcb;
  reg_field#(4) f_mode, f_speed;
  reg_field#(8) f_div, f_code;
  int errs;
  int nkeys;
  initial begin
    blk = new;
    ctrl = new("CTRL"); status = new("STATUS");
    f_mode = new("MODE", 0); ctrl.add4(f_mode);
    f_speed = new("SPEED", 4, 4'h3); ctrl.add4(f_speed);
    f_div = new("DIV", 8, 8'h10); ctrl.add8(f_div);
    f_code = new("CODE", 0); status.add8(f_code);
    blk.add(ctrl); blk.add(status);
    lcb = new; mcb = new(32'h0000_00f0);
    ctrl.cbs.push_back(lcb); ctrl.cbs.push_back(mcb); status.cbs.push_back(lcb);
    blk.write("CTRL", 32'h1234_5a1b);
    blk.write("CTRL", 32'h0000_0a1b);
    blk.write("STATUS", 32'h0000_00c3);
    blk.write("NOPE", 1);
    if (mask_log.size() != 1) begin $error("mask_log size %0d", mask_log.size()); errs++; end
    else if (mask_log[0] != "CTRL changed masked bits 00000010") begin $error("mask_log '%s'", mask_log[0]); errs++; end
    if (nosuch != 1) begin $error("nosuch %0d", nosuch); errs++; end
    if (blk.order[0].describe() != "CTRL MODE=b SPEED=1 DIV=a") begin $error("describe0 '%s'", blk.order[0].describe()); errs++; end
    if (blk.order[1].describe() != "STATUS CODE=c3") begin $error("describe1 '%s'", blk.order[1].describe()); errs++; end
    if (lcb.count != 3) begin $error("count %0d", lcb.count); errs++; end
    if (lcb.last != "STATUS:0->c3") begin $error("last '%s'", lcb.last); errs++; end
    if (ctrl.f4[1].get_mask() !== 32'h000000f0) begin $error("mask4"); errs++; end
    if (ctrl.f8[0].get_mask() !== 32'h0000ff00) begin $error("mask8"); errs++; end
    if (ctrl.f4[1].reset_v !== 4'h3) begin $error("reset4"); errs++; end
    if (ctrl.f8[0].reset_v !== 8'h10) begin $error("reset8"); errs++; end
    // iteration order of an associative array is not specified: check as a set
    foreach (blk.regs[k]) begin
      nkeys++;
      if (k == "CTRL") begin if (blk.regs[k].read() !== 32'h0a1b) begin $error("CTRL value"); errs++; end end
      else if (k == "STATUS") begin if (blk.regs[k].read() !== 32'hc3) begin $error("STATUS value"); errs++; end end
      else begin $error("unexpected key %s", k); errs++; end
    end
    if (nkeys != 2) begin $error("nkeys %0d", nkeys); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
