entity aggtarget3 is
end entity;

architecture test of aggtarget3 is
    type vec is array (0 to 2) of integer;

    procedure swap (x : inout vec) is
    begin
        x := (x(1) + 10, x(0) + 20, x(2));
    end procedure;
begin

    p1: process is
        variable a, b, c : integer;
    begin
        -- Named choices in an aggregate target are not in index order
        (2 => a, 0 => b, 1 => c) := vec'(1, 2, 3);
        assert b = 1 and c = 2 and a = 3 report "assignment";

        a := 1;
        b := 2;
        c := 3;
        -- Partial association of an inout parameter
        swap (x(1) => a, x(0) => b, x(2) => c);
        assert b = 11 and a = 22 and c = 3
            report integer'image(a) & " " & integer'image(b);
        wait;
    end process;

end architecture;
