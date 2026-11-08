// Expected values: IEEE 1800-2017 27.2 (conditional generate; only the selected branch is elaborated),
// 20.11 (elaboration system tasks: the $error/$fatal in the unselected branches must not fire),
// 21.2.1.6 (%m hierarchical name of the scope).
module sub #(parameter int DW = 4);
  string w, o;
  if (DW == 4) begin : g4
    initial w = $sformatf("%m");
  end else if (DW == 8) begin : g8
    initial w = $sformatf("%m");
  end else begin : g_invalid
    $error("%m: Invalid width");
  end
  if (DW != 4 && DW != 8) $fatal(1, "%m: bad width %0d", DW);
  if (DW > 100) $warning("never");
  else begin : g_ok
    initial o = $sformatf("%m");
  end
endmodule
module svgenerrtask1;
  sub #(4) a();
  sub #(8) b();
  int bad = 0;
  initial begin
    #1;
    if (a.w != "svgenerrtask1.a.g4") begin $error("a.w=%s", a.w); bad++; end
    if (b.w != "svgenerrtask1.b.g8") begin $error("b.w=%s", b.w); bad++; end
    if (a.o != "svgenerrtask1.a.g_ok") begin $error("a.o=%s", a.o); bad++; end
    if (b.o != "svgenerrtask1.b.g_ok") begin $error("b.o=%s", b.o); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
