// Handle comparisons on associative arrays indexed by non-rand class
// properties (packed struct / string) and string-keyed integral associative
// arrays indexed by non-rand string expressions in constraints.
typedef struct packed {
  bit [3:0] hi;
  bit [3:0] lo;
} key_t;

class item_c;
  rand int val;
endclass

class struct_key_c;
  rand item_c items[key_t];
  key_t k1;
  key_t k2;
  constraint c { items[k1] != items[k2]; }
endclass

class str_key_c;
  rand item_c items[string];
  string s1;
  string s2;
  constraint c { items[s1] != items[s2]; }
endclass

class str_arr_key_c;
  rand int vals[string];
  string keys[2] = '{"alpha", "beta"};
  function new();
    vals["alpha"] = 0;
    vals["beta"] = 0;
  endfunction
  constraint c {
    vals[keys[0]] > 10;
    vals[keys[1]] < 0;
  }
endclass

module svcrvassocobj3;
  int bad;

  initial begin
    automatic struct_key_c sk = new;
    automatic item_c shared = new;
    sk.k1 = 8'h11;
    sk.k2 = 8'h22;
    sk.items[8'h11] = new;
    sk.items[8'h22] = new;
    if (!sk.randomize()) bad++;

    sk.items[8'h11] = shared;
    sk.items[8'h22] = shared;
    if (sk.randomize()) bad++;

    begin
      automatic str_key_c st = new;
      st.s1 = "one";
      st.s2 = "two";
      st.items["one"] = new;
      st.items["two"] = new;
      if (!st.randomize()) bad++;
      st.items["two"] = st.items["one"];
      if (st.randomize()) bad++;
    end

    begin
      automatic str_arr_key_c sa = new;
      repeat (5) begin
        if (!sa.randomize()) bad++;
        if (!(sa.vals["alpha"] > 10 && sa.vals["beta"] < 0)) bad++;
      end
    end

    if (bad == 0)
      $display("PASSED");
    else
      $fatal(1, "FAILED %0d", bad);
  end
endmodule
