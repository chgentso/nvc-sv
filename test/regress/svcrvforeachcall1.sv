package svcrvforeachcall1_pkg;
  function automatic logic [3:0] enc_bit(bit b);
    return b ? 4'h9 : 4'h6;
  endfunction

  typedef struct packed {
    logic [3:0] en;
    logic [3:0] mode;
  } region_t;

  typedef struct {
    rand logic [3:0] en;
    rand int unsigned page;
  } unpacked_region_t;

  class cfg_c;
    bit [3:0] en_mask;
    rand region_t regions[4];
    rand unpacked_region_t uregions[4];

    constraint regions_c {
      foreach (regions[i]) {
        regions[i].en == enc_bit(en_mask[i]);
        regions[i].mode inside {[4'h1:4'h3]};
      }
      foreach (uregions[i]) {
        uregions[i].en == enc_bit(en_mask[i]);
        uregions[i].page inside {[0:7]};
        foreach (uregions[j]) {
          if (i != j) {
            uregions[i].page != uregions[j].page;
          }
        }
      }
    }
  endclass
endpackage

module svcrvforeachcall1;
  import svcrvforeachcall1_pkg::*;

  initial begin
    automatic cfg_c c = new();
    c.en_mask = 4'b1010;
    if (!c.randomize()) $fatal(1, "randomize failed");
    for (int i = 0; i < 4; i++) begin
      automatic logic [3:0] exp = enc_bit(c.en_mask[i]);
      if (c.regions[i].en !== exp)
        $fatal(1, "regions[%0d].en=%h exp=%h", i, c.regions[i].en, exp);
      if (c.regions[i].mode < 4'h1 || c.regions[i].mode > 4'h3)
        $fatal(1, "regions[%0d].mode=%h out of range", i, c.regions[i].mode);
      if (c.uregions[i].en !== exp)
        $fatal(1, "uregions[%0d].en=%h exp=%h", i, c.uregions[i].en, exp);
      for (int j = 0; j < 4; j++) begin
        if (i != j && c.uregions[i].page == c.uregions[j].page)
          $fatal(1, "duplicate page %0d", c.uregions[i].page);
      end
    end
    $display("PASSED");
  end
endmodule
