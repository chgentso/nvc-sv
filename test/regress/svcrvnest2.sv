// Same for a member of an element of an array of queues
typedef struct packed { bit [1:0] en; bit rd; } cfg_t;
class c;
  rand cfg_t pages[2][3][$];
  constraint e { foreach (pages[i,j,k]) pages[i][j][k].en == 2'(i + 1); }
endclass

module svcrvnest2;
  c o;
  initial begin
    o = new;
    void'(o.randomize());
  end
endmodule
