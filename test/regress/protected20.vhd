-- Method calls through accesses to an uninstantiated protected type:
-- the allocator's generic map aspect creates the object and calls are
-- dispatched to that instance (functions, procedures with default
-- parameters, string parameters and results, arrays of access values,
-- allocators in subprograms) (LCS-2016-014a, IEEE 1076-2019 5.4.1, 9.3.7).

entity protected20 is end entity;
architecture a of protected20 is
    type acc_t is protected
        generic ( type t; init : t; scale : integer := 1 );
        procedure add (n : integer; by : integer := 1);
        impure function total return integer;
        impure function name_of (prefix : string) return string;
        impure function val return t;
    end protected;
    type acc_t is protected body
        variable sum : integer := 0;
        variable v : t := init;
        procedure add (n : integer; by : integer := 1) is
        begin
            sum := sum + n * by * scale;
        end procedure;
        impure function total return integer is begin return sum; end function;
        impure function name_of (prefix : string) return string is
        begin return prefix & "!"; end function;
        impure function val return t is begin return v; end function;
    end protected body;

    type acc_ptr is access acc_t;
    type acc_arr is array (natural range <>) of acc_ptr;

    procedure use_it (variable p : acc_ptr) is
        variable q : acc_ptr := new acc_t generic map (t => real, init => 1.5, scale => 3);
    begin
        p.add(1);
        q.add(2, 2);
        assert q.total = 12;
        assert p.total = 10 + 1 report integer'image(p.total);
        assert q.name_of("abc") = "abc!";
        deallocate(q);
    end procedure;
begin
    process
        variable a : acc_arr(1 to 2);
        variable x : acc_ptr;
    begin
        a(1) := new acc_t generic map (t => integer, init => 3);
        a(2) := new acc_t generic map (t => bit, init => '1', scale => 10);
        a(1).add(10);
        a(2).add(1, 5);
        assert a(1).total = 10;
        assert a(2).total = 50;
        x := a(1);
        use_it(x);
        assert a(1).total = 11;
        assert a(2).name_of("x") = "x!";
        x := null;
        deallocate(a(1));
        deallocate(a(2));
        wait;
    end process;
end architecture;
