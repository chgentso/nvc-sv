-- The combinational processes of an RTL island only run when a signal they
-- read changed.  Every output must still change in the same delta cycle as
-- without islands: partial fan-out, reconvergent paths, a process reading
-- its own output, a direct wire from another island, a forced signal in
-- the middle of a chain and its release.

library ieee;
use ieee.numeric_std.all;

entity vhdl_island_act1_stage is
    port (i : in unsigned(7 downto 0); o : out unsigned(7 downto 0));
end entity;

architecture rtl of vhdl_island_act1_stage is
    signal t : unsigned(7 downto 0);
begin
    p_inc : process (all) is
    begin
        t <= i + 3;
    end process;

    p_xor : process (all) is
    begin
        o <= t + 7;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_act1_dut is
    port (
        clk           : in  std_logic;
        a, e          : in  unsigned(7 downto 0);
        d, g, k, v, z : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of vhdl_island_act1_dut is
    signal q, b, c, d_i, f, h, w1, zi : unsigned(7 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= q + 16;
        end if;
    end process;

    -- Chain from a
    p_b : process (all) is
    begin
        b <= a + 1;
    end process;

    p_c : process (all) is
    begin
        c <= b + 1;
    end process;

    p_d : process (all) is
    begin
        d_i <= c + 1;
    end process;

    -- Chain from e and the register
    p_f : process (all) is
    begin
        f <= e + 1;
    end process;

    p_g : process (all) is
    begin
        g <= f xor q;
    end process;

    -- Reconvergent paths of different depth
    p_h : process (all) is
    begin
        h <= b + f;
    end process;

    p_k : process (all) is
    begin
        k <= h + d_i;
    end process;

    -- Reads its own output: counts up one delta at a time
    p_z : process (all) is
    begin
        if zi < a(3 downto 0) then
            zi <= zi + 1;
        elsif zi > a(3 downto 0) then
            zi <= zi - 1;
        end if;
    end process;

    -- Input that only changes through a direct wire from another island
    u_st : entity work.vhdl_island_act1_stage port map (e, w1);

    p_v : process (all) is
    begin
        v <= w1 + c;
    end process;

    d <= d_i;
    z <= zi;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_act1 is
end entity;

architecture test of vhdl_island_act1 is
    signal clk           : std_logic := '0';
    signal a, e          : unsigned(7 downto 0) := (others => '0');
    signal d, g, k, v, z : unsigned(7 downto 0);
    signal step, tick    : natural := 0;
begin
    dut : entity work.vhdl_island_act1_dut
        port map (clk, a, e, d, g, k, v, z);

    p_log : process (d, g, k, v, z) is
        procedure show (name : string; s : unsigned) is
        begin
            report integer'image(step) & "." & integer'image(tick) & " "
                & name & "=" & to_hstring(s);
        end procedure;
    begin
        if d'event then show("d", d); end if;
        if g'event then show("g", g); end if;
        if k'event then show("k", k); end if;
        if v'event then show("v", v); end if;
        if z'event then show("z", z); end if;
    end process;

    p_stim : process is
        procedure settle is
        begin
            for i in 1 to 12 loop
                tick <= i;
                wait for 0 ns;
            end loop;
            wait for 1 ns;
            tick <= 0;
            step <= step + 1;
            wait for 1 ns;
        end procedure;
    begin
        wait for 1 ns;
        step <= 1;
        wait for 1 ns;
        a <= x"05";                     -- 1: chain from a only
        settle;
        e <= x"20";                     -- 2: chain from e only
        settle;
        a <= x"03";                     -- 3: both
        e <= x"40";
        settle;
        clk <= '1';                     -- 4: register only
        settle;
        clk <= '0';
        << signal dut.b : unsigned(7 downto 0) >> <= force x"55";  -- 5
        settle;
        a <= x"09";                     -- 6: forced b holds
        e <= x"41";
        settle;
        << signal dut.b : unsigned(7 downto 0) >> <= release;      -- 7
        settle;
        a <= x"0a";                     -- 8
        settle;
        report "done";
        wait;
    end process;
end architecture;
