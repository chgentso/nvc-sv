-- Combinational chains inside and between RTL islands must keep the delta
-- cycle of every value change: a VHDL signal assignment takes effect one
-- delta after the process runs, also when both processes are fused.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_delta1_stage is
    port (i : in unsigned(7 downto 0); o : out unsigned(7 downto 0));
end entity;

architecture rtl of vhdl_island_delta1_stage is
begin
    p_inc : process (all) is
    begin
        o <= i + 1;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_delta1 is
end entity;

architecture test of vhdl_island_delta1 is
    signal clk  : std_logic := '0';
    signal sel  : integer range 2 to 3 := 3;
    signal tick : natural := 0;         -- delta cycle after the stimulus
    signal q    : unsigned(7 downto 0) := (others => '0');
    signal a, b, c, d : unsigned(7 downto 0) := (others => '0');
    signal x    : std_logic := '0';
    signal r, s, t : unsigned(7 downto 0) := (others => '0');
    signal w1, w2, w3 : unsigned(7 downto 0) := (others => '0');
    signal done : boolean := false;
    constant EXPECT : string :=
        "w0=XX c0=01 t0=01 t0=03 w0=03 t0=05 "
        & "| c3=07 t3=0A w4=08 t6=10 "
        & "| c3=0D t3=15 w4=0D t6=1B "
        & "| c3=13 t3=20 w4=12 t6=26 ";
begin
    -- Register and a chain of combinational processes in one island
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= q + 1;
        end if;
    end process;

    p_b : process (all) is
    begin
        b <= a + 1;
    end process;

    p_c : process (all) is
    begin
        c <= b + q;
    end process;

    -- This process is rejected while lowering the island ('val of a
    -- non-constant) and runs as an ordinary process between fused ones
    p_bad : process (all) is
    begin
        if c(0) = '1' then
            x <= std_logic'val(sel);
        else
            x <= '0';
        end if;
        r <= c + 2;
    end process;

    p_s : process (all) is
    begin
        if x = '1' then
            s <= r + 1;
        else
            s <= r;
        end if;
    end process;

    p_t : process (all) is
    begin
        t <= s + b;
    end process;

    -- A chain of instances joined by direct wires
    u1 : entity work.vhdl_island_delta1_stage port map (a, w1);
    u2 : entity work.vhdl_island_delta1_stage port map (w1, w2);
    u3 : entity work.vhdl_island_delta1_stage port map (w2, w3);

    -- Records the delta cycle (tick) of every change of c, t and w3
    p_log : process (c, t, w3, tick, done) is
        variable log : string(1 to 1024);
        variable len : natural := 0;

        procedure add (str : string) is
        begin
            log(len + 1 to len + str'length) := str;
            len := len + str'length;
        end procedure;
    begin
        if tick'event and tick = 1 then
            add("| ");
        end if;
        if c'event then
            add("c" & integer'image(tick) & "=" & to_hstring(c) & " ");
        end if;
        if t'event then
            add("t" & integer'image(tick) & "=" & to_hstring(t) & " ");
        end if;
        if w3'event then
            add("w" & integer'image(tick) & "=" & to_hstring(w3) & " ");
        end if;
        if done then
            assert log(1 to len) = EXPECT
                report "got: " & log(1 to len) severity failure;
        end if;
    end process;

    p_stim : process is
    begin
        wait for 1 ns;
        for n in 1 to 3 loop
            a <= a + 5;
            clk <= '1';
            for i in 1 to 8 loop
                tick <= i;
                wait for 0 ns;
            end loop;
            wait for 1 ns;
            clk <= '0';
            wait for 1 ns;
        end loop;
        done <= true;
        wait;
    end process;
end architecture;
