library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_generic1_leaf is
    generic (
        HART_ID : natural range 0 to 3
    );
    port (
        clk   : in  std_logic;
        sel   : in  std_logic;
        rdata : out std_ulogic_vector(7 downto 0)
    );
end entity;

architecture rtl of vhdl_island_generic1_leaf is
    signal q : std_ulogic_vector(7 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if sel = '1' then
                q <= std_ulogic_vector(to_unsigned(HART_ID, 8));
            else
                q <= x"FF";
            end if;
        end if;
    end process;

    rdata <= q;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_generic1_mid is
    generic (
        HART_ID : natural range 0 to 3 := 0
    );
    port (
        clk   : in  std_logic;
        sel   : in  std_logic;
        rdata : out std_ulogic_vector(7 downto 0)
    );
end entity;

architecture rtl of vhdl_island_generic1_mid is
begin
    u_leaf : entity work.vhdl_island_generic1_leaf
        generic map (
            HART_ID => HART_ID
        )
        port map (
            clk   => clk,
            sel   => sel,
            rdata => rdata
        );
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_generic1 is
end entity;

architecture test of vhdl_island_generic1 is
    type vec_arr_t is array (0 to 1) of std_ulogic_vector(7 downto 0);
    signal clk  : std_logic := '0';
    signal sel  : std_logic := '1';
    signal outs : vec_arr_t := (others => (others => '0'));
    signal done : boolean := false;
begin
    gen_core : for i in 0 to 1 generate
        u_mid : entity work.vhdl_island_generic1_mid
            generic map (
                HART_ID => i
            )
            port map (
                clk   => clk,
                sel   => sel,
                rdata => outs(i)
            );
    end generate;

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

    p_stim : process is
    begin
        sel <= '1';
        wait until rising_edge(clk);
        wait for 1 ns;
        assert outs(0) = x"00" severity failure;
        assert outs(1) = x"01"
            report "core 1 HART_ID wrong: got " & to_hstring(outs(1))
            severity failure;
        done <= true;
        wait;
    end process;
end architecture;
