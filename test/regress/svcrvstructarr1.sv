// Self-checking: rand struct arrays (dynamic array of structs with rand members), foreach constraints,
// references to struct members of a queue element, and non-random struct members.
// Expected values: IEEE 1800-2017 18.5 (constraints, foreach 18.5.8), 18.4 (rand and non-rand variables:
// a non-random variable is not changed by randomize()), 7.2 (structures), 7.5 (dynamic arrays).
// Implementation-defined/unspecified and therefore not checked: the contents of elements when randomize()
// changes the size of cfg.
typedef enum bit [1:0] {OFF, TOR, NA4, NAPOT} mode_t;
typedef struct {
  rand bit l;
  rand mode_t a;
  rand bit [7:0] addr;
  rand int offset;
  bit [3:0] fixed;
  string name;
} reg_t;
typedef struct { bit l; int offset; } pair_t;
typedef struct {
  string name;
  int unsigned size_in_bytes;
  bit [2:0] xwr;
} mem_t;

class pmp;
  rand int n;
  rand reg_t cfg[];
  rand bit mml;
  constraint c1 { n inside {[2:5]}; cfg.size() == n; }
  constraint c2 {
    foreach (cfg[i]) {
      cfg[i].offset inside {[10:20]};
      if (i == 0) cfg[i].a == TOR;
      if (i > 0 && cfg[i].a == TOR) cfg[i].addr != cfg[i-1].addr;
      cfg[i].addr[7:6] == 2'b00;
    }
  }
endclass

class stream;
  mem_t pages[$];
  rand int unsigned page_id;
  rand int unsigned max_off;
  rand int unsigned base;
  constraint c {
    page_id < pages.size();
    foreach (pages[i]) {
      if (i == page_id) max_off == pages[i].size_in_bytes;
    }
    base inside {[0 : max_off - 1]};
  }
endclass

module svcrvstructarr1;
  initial begin
    automatic pmp p = new;
    automatic stream s = new;
    automatic bit ok = 1;
    mem_t m;
    p.cfg = new[2];
    p.cfg[0].name = "keep0";
    p.cfg[0].fixed = 5;
    repeat (30) begin
      automatic int oldsz = p.cfg.size();
      if (!p.randomize()) begin ok = 0; $error("randomize of pmp failed"); end
      else begin
        if (p.cfg.size() != p.n || p.n < 2 || p.n > 5) begin ok = 0; $error("size %0d %0d", p.cfg.size(), p.n); end
        // non-random members keep their values when the array is not resized
        if (p.cfg.size() == oldsz && (p.cfg[0].name != "keep0" || p.cfg[0].fixed != 5)) begin
          ok = 0; $error("non-random members changed: %s %0d", p.cfg[0].name, p.cfg[0].fixed);
        end
        foreach (p.cfg[i]) begin
          if (!(p.cfg[i].offset inside {[10:20]}) || p.cfg[i].addr[7:6] != 0) begin ok = 0; $error("c%0d %0d %0d", i, p.cfg[i].offset, p.cfg[i].addr); end
          if (i == 0 && p.cfg[i].a != TOR) ok = 0;
          if (i > 0 && p.cfg[i].a == TOR && p.cfg[i].addr == p.cfg[i-1].addr) begin ok = 0; $error("tor %0d", i); end
        end
      end
    end
    m.name = "a"; m.size_in_bytes = 100; m.xwr = 1; s.pages.push_back(m);
    m.name = "b"; m.size_in_bytes = 7; m.xwr = 2; s.pages.push_back(m);
    m.name = "c"; m.size_in_bytes = 4096; m.xwr = 3; s.pages.push_back(m);
    repeat (50) begin
      if (!s.randomize()) begin ok = 0; $error("randomize of stream failed"); end
      else if (s.page_id >= 3 || s.max_off != s.pages[s.page_id].size_in_bytes || s.base >= s.max_off) begin
        ok = 0; $error("BAD %0d %0d %0d", s.page_id, s.max_off, s.base);
      end
    end
    if (ok) $display("PASSED");
  end
endmodule
