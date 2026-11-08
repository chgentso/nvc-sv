module crv3;

  real rv;
  int res;
  int x;

  class inner;
    rand int v;
  endclass

  class bad;
    rand real r;
    randc int wide;
    randc inner h;
    rand string s;
    rand inner ok;
    int plain;
    constraint k1 { ok; }
    constraint k2 { if (r) plain == 1; }
    constraint k3 { plain inside {rv, [1:2]}; }
    constraint k4 { soft plain dist { 1 := 1 }; }
    constraint k5 { plain inside {s}; }
    constraint k6 { plain == 1 -> rv > 1; }
  endclass

  bad b = new;

  initial begin
    res = std::randomize(rv);
    res = std::randomize(x + 1);
    if (1 inside {rv}) res = 1;
    if (b inside {1}) res = 1;
    randcase
      rv: res = 1;
    endcase
  end

endmodule
