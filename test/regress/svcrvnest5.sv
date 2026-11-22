// A weight which depends on the position in a queue is not supported
class c;
  rand int pages[2][$];
  constraint s {
    foreach (pages[i]) pages[i].size() == 3;
    foreach (pages[i, j]) pages[i][j] dist { 0 :/ j, 1 :/ 3 };
  }
endclass

module svcrvnest5;
  c o;
  initial begin
    o = new;
    void'(o.randomize());
  end
endmodule
