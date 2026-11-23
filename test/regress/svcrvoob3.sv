// A constraint on a fixed element of a rand dynamic array of structures applies when the size the
// solver chooses includes the element.
typedef struct { rand int value; } Entry;

class A;
  rand Entry values[];
  constraint c_size { values.size() == 3; }
  constraint c_values {
    values[0].value == 0;
    values[1].value == 31;
    values[2].value == 15;
  }
endclass

class B;
  rand Entry values[];
  constraint c_size { values.size() == 1; }
  constraint c_value { values[1].value == 31; }
endclass

module svcrvoob3;
  initial begin
    A a = new;
    B b = new;
    repeat (10) begin
      if (a.randomize() != 1) $error("randomize failed");
      if (a.values.size() != 3 || a.values[0].value != 0
          || a.values[1].value != 31 || a.values[2].value != 15)
        $error("values %p", a.values);
    end
    if (b.randomize() != 0) $error("element 1 of 1");
    $display("PASSED");
  end
endmodule
