library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dw_prod is
    port (
        clk  : in  std_logic;
        rst  : in  std_logic;
        en   : in  std_logic;
        dout : out std_logic_vector(7 downto 0);
        flag : out std_logic
    );
end entity;

architecture rtl of dw_prod is
    signal cnt  : unsigned(7 downto 0) := (others => '0');
    signal next_cnt : unsigned(7 downto 0);
begin
    p_comb : process (all) is
    begin
        if en = '1' then
            next_cnt <= cnt + 1;
        else
            next_cnt <= cnt;
        end if;
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                cnt <= (others => '0');
            else
                cnt <= next_cnt;
            end if;
        end if;
    end process;

    dout <= std_logic_vector(cnt);
    flag <= cnt(0);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dw_mid is
    port (
        din  : in  std_logic_vector(7 downto 0);
        fin  : in  std_logic;
        dout : out std_logic_vector(7 downto 0);
        fout : out std_logic
    );
end entity;

architecture rtl of dw_mid is
begin
    dout <= din;
    fout <= fin;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dw_cons is
    port (
        clk  : in  std_logic;
        rst  : in  std_logic;
        din  : in  std_logic_vector(7 downto 0);
        fin  : in  std_logic;
        acc  : out std_logic_vector(7 downto 0);
        par  : out std_logic
    );
end entity;

architecture rtl of dw_cons is
    signal reg_v  : unsigned(7 downto 0) := (others => '0');
    signal next_v : unsigned(7 downto 0);
    signal reg_p  : std_logic := '0';
begin
    p_comb : process (all) is
    begin
        next_v <= unsigned(din) + x"10";
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                reg_v <= (others => '0');
                reg_p <= '0';
            else
                reg_v <= next_v;
                reg_p <= fin;
            end if;
        end if;
    end process;

    acc <= std_logic_vector(reg_v);
    par <= reg_p;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_direct_wire1 is
end entity;

architecture test of vhdl_island_direct_wire1 is
    signal clk      : std_logic := '0';
    signal rst      : std_logic := '1';
    signal en       : std_logic := '0';
    signal bus_a    : std_logic_vector(7 downto 0);
    signal flag_a   : std_logic;
    signal bus_b    : std_logic_vector(7 downto 0);
    signal flag_b   : std_logic;
    signal acc      : std_logic_vector(7 downto 0);
    signal par      : std_logic;
    signal done     : boolean := false;
begin
    u_prod : entity work.dw_prod
        port map (
            clk  => clk,
            rst  => rst,
            en   => en,
            dout => bus_a,
            flag => flag_a
        );

    u_mid : entity work.dw_mid
        port map (
            din  => bus_a,
            fin  => flag_a,
            dout => bus_b,
            fout => flag_b
        );

    u_cons : entity work.dw_cons
        port map (
            clk  => clk,
            rst  => rst,
            din  => bus_b,
            fin  => flag_b,
            acc  => acc,
            par  => par
        );

    p_clk : process is
    begin
        while not done loop
            clk <= '0';
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
        end loop;
        wait;
    end process;

    p_check : process is
    begin
        wait until rising_edge(clk);
        wait for 1 ns;
        rst <= '0';
        en  <= '1';

        -- Producer counts 1, 2, 3, 4; consumer sees delayed value + 0x10
        for i in 1 to 5 loop
            wait until rising_edge(clk);
            wait for 1 ns;
            assert acc = std_logic_vector(to_unsigned((i - 1) + 16, 8))
                report "mismatch at step " & integer'image(i)
                severity failure;
            assert par = std_logic(to_unsigned(i - 1, 8)(0))
                severity failure;
        end loop;

        -- Force on intermediate parent signal must break direct wire cleanly
        bus_a <= force x"a0";
        wait until rising_edge(clk);
        wait for 1 ns;
        assert acc = x"b0" severity failure;

        bus_a <= release;
        wait until rising_edge(clk);
        wait for 1 ns;
        -- Producer kept counting (at step 5 -> 6, then 7 on next edge)
        assert acc = std_logic_vector(to_unsigned(6 + 16, 8)) severity failure;

        done <= true;
        wait;
    end process;
end architecture;
