// Module ports of type event, string, class handle and dynamic array
// Expected values: IEEE 1800-2017 23.2.2 (a port may have any data type, and an
// event input names the event of its actual: 15.5, 15.5.1), 23.3.3.2 (an input
// port is a continuous assignment from the actual, an output port the reverse)
class box; int v = 7; endclass

module svevtport1_sub(input event ev, input string s, input box h, input int q[],
                      output int n, output int len, output string back);
  always @(s) len = s.len();
  always @(ev) n++;
  initial begin
    #3 back = {s, "!"};
  end
  int total;
  always @(q) total = q.size() + h.v;
endmodule

module svevtport1;
  event e;
  string str = "hello";
  box b = new;
  int d[] = new[3];
  int n = 0, len = 0;
  string back;
  int errors = 0;
  svevtport1_sub u(.ev(e), .s(str), .h(b), .q(d), .n(n), .len(len), .back(back));
  initial begin
    #1 -> e;
    #1 -> e;
    str = "hi";
    d = new[5];
    #5;
    if (n != 2) begin $error("n = %0d", n); errors++; end
    if (len != 2) begin $error("len = %0d", len); errors++; end
    if (back != "hi!") begin $error("back = %s", back); errors++; end
    if (u.total != 12) begin $error("total = %0d", u.total); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
