// Self-checking. Expected values: IEEE 1800-2017 11.4.13 (the set membership
// operator inside is true if the expression equals any element of a set; an
// element of the set may be an array, a dynamic array or queue, whose elements
// are all members of the set), 6.16 (strings are compared by their characters).
module svinsidestr1;
  string dyn[] = '{"x", "y"};
  string q[$] = '{"p", "q"};
  string lst[];
  string n = "fatal_fault", m = "nope", s = "q";
  int errs = 0;
  initial begin
    lst = new[2];
    lst[0] = "recov_ctrl_update_err";
    lst[1] = "fatal_fault";
    if (!(n inside {lst})) begin errs++; $error("n inside lst"); end
    if (m inside {lst}) begin errs++; $error("m inside lst"); end
    if (n inside {dyn}) begin errs++; $error("n inside dyn"); end
    if (!(s inside {q})) begin errs++; $error("s inside q"); end
    if (!(n inside {"a", lst, "b"})) begin errs++; $error("mixed set"); end
    if (!(n inside {"fatal_fault", "z"})) begin errs++; $error("literal set"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
