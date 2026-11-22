// A with clause which is an inside test selects intervals without
// visiting every value and gives the same bins as enumerating them
module vlog_cov_with_inside;
  localparam bit [7:0] T = 'h5, F = 'ha;
  localparam bit [31:0] BIG = '1;

  covergroup ga with function sample(bit [7:0] v);
    cp: coverpoint v {
      bins t = {T};
      bins o[7] = {[0:255]} with (!(item inside {T, F, [100:120]}));
      bins p[3] = {[0:255]} with (item inside {[10:19], 30, [200:249]});
      bins q = {[0:255]} with (item inside {[250:300]});
    }
  endgroup

  // The same selections written so that every value is visited
  covergroup gb with function sample(bit [7:0] v);
    cp: coverpoint v {
      bins t = {T};
      bins o[7] = {[0:255]} with (item != T && item != F && !(item >= 100 && item <= 120));
      bins p[3] = {[0:255]} with ((item >= 10 && item <= 19) || item == 30 || (item >= 200 && item <= 249));
      bins q = {[0:255]} with (item >= 250 && item <= 300);
    }
  endgroup

  // A range as wide as a 32 bit value is not enumerated
  covergroup gw with function sample(bit [31:0] v);
    cp: coverpoint v {
      bins t = {32'h5};
      bins o[32] = {[0:BIG]} with (!(item inside {32'h5, 32'ha}));
    }
  endgroup

  // A name in the list may be an array whose elements are the values
  int arr[] = '{40, 41, 42, 43};
  covergroup gc with function sample(bit [7:0] v);
    cp: coverpoint v {
      bins a[4] = {[0:255]} with (item inside {arr});
    }
  endgroup

  ga a = new;
  gc c = new;
  gb b = new;
  gw w = new;
  int bad;

  initial begin
    for (int i = 0; i < 256; i += 3) begin
      a.sample(i);
      b.sample(i);
      if (a.get_inst_coverage() != b.get_inst_coverage()) begin
        $display("MISMATCH at %0d: %f %f", i, a.get_inst_coverage(),
                 b.get_inst_coverage());
        bad++;
      end
    end
    if (a.get_inst_coverage() <= 0.0) bad++;
    c.sample(41);
    if (c.get_inst_coverage() != 25.0) begin
      $display("array list: %f", c.get_inst_coverage());
      bad++;
    end
    w.sample(32'h5);
    w.sample(32'hffff_fff0);
    if (w.get_inst_coverage() <= 0.0 || w.get_inst_coverage() >= 100.0) bad++;
    if (bad == 0) $display("PASSED");
    else $display("FAILED");
  end
endmodule
