class self_ext extends self_ext;
endclass

typedef class b;
typedef class c;

class a extends b;
endclass

class b extends c;
endclass

class c extends a;
endclass

module top;
endmodule
