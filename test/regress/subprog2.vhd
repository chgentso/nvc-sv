entity subprog2 is
end entity;

architecture test of subprog2 is
    signal res : integer := 0;

    procedure acc(x : integer; val : integer := 0; signal s : out integer) is
    begin
        s <= x + val;
    end procedure;

    procedure acc(x, y : integer; val : integer := 0; signal s : out integer) is
    begin
        s <= x * 10 + y + val;
    end procedure;

    procedure acc(x, y, z : integer; val : integer := 0; signal s : out integer) is
    begin
        s <= x * 100 + y * 10 + z + val;
    end procedure;
begin
    check : process is
    begin
        acc(1, val => 2, s => res);
        wait for 1 ns;
        assert res = 3;

        acc(1, 2, val => 3, s => res);
        wait for 1 ns;
        assert res = 15;

        acc(1, 2, 3, val => 4, s => res);
        wait for 1 ns;
        assert res = 127;
        wait;
    end process;
end architecture;
