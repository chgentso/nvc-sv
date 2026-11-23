entity nestedpkg3_inst is
    generic (SEED : integer);
    port (done_val : out integer);
end entity;

architecture rtl of nestedpkg3_inst is
begin
    process is
        variable pv : integer := SEED;
        package state_pkg is
            variable acc : integer := pv;
            procedure bump_wait(d : integer);
            impure function total return integer;
        end package;
        package body state_pkg is
            variable hidden : integer := 100;
            procedure bump_wait(d : integer) is
            begin
                wait for 1 ns;
                pv := pv + d;
                acc := acc + pv;
                hidden := hidden + 1;
            end procedure;
            impure function total return integer is
            begin
                return acc + hidden;
            end function;
        end package body;
    begin
        state_pkg.acc := state_pkg.acc + 1;
        state_pkg.bump_wait(2);
        state_pkg.bump_wait(3);
        done_val <= state_pkg.total;
        wait;
    end process;
end architecture;

entity nestedpkg3 is
end entity;

architecture test of nestedpkg3 is
    signal clk : bit := '0';
    signal v1, v2 : integer;
    signal ticks : integer := 0;
begin
    clk <= not clk after 5 ns when now < 15 ns;

    sens: process(clk) is
        package p is
            variable count : integer := 0;
            variable arr : integer_vector(1 to 2) := (0, 0);
        end package;
    begin
        p.count := p.count + 1;
        p.arr(1) := p.count;
        p.arr(2) := p.count * 2;
        assert p.arr(2) = p.arr(1) * 2;
        ticks <= p.count;
    end process;

    u1: entity work.nestedpkg3_inst generic map (SEED => 1) port map (done_val => v1);
    u2: entity work.nestedpkg3_inst generic map (SEED => 10) port map (done_val => v2);

    check: process is
    begin
        wait for 20 ns;
        -- u1: pv=1 -> acc=1 -> acc=2 -> bump(2): pv=3,acc=5,h=101 -> bump(3): pv=6,acc=11,h=102 -> 113
        -- u2: pv=10 -> acc=10 -> acc=11 -> bump(2): pv=12,acc=23,h=101 -> bump(3): pv=15,acc=38,h=102 -> 140
        assert v1 = 113 report "v1=" & integer'image(v1);
        assert v2 = 140 report "v2=" & integer'image(v2);
        assert ticks = 4 report "ticks=" & integer'image(ticks);
        wait;
    end process;
end architecture;
