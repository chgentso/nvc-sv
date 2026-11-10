library ieee;
use ieee.std_logic_1164.all;

entity leaf is
    generic ( N : natural := 2 );
    port ( a  : in std_logic_vector(N - 1 downto 0);
           b  : out std_logic_vector(N - 1 downto 0);
           io : inout std_logic );
end entity;

architecture rtl of leaf is
    signal tmp : std_logic_vector(N - 1 downto 0);
begin
    tmp <= not a;

    p_out: process (tmp) is
    begin
        b <= tmp;
    end process;

    io <= 'Z';
end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity vhdl1 is
end entity;

architecture test of vhdl1 is
    signal clk  : std_logic := '0';
    signal d, q : std_logic_vector(3 downto 0);
    signal e    : std_logic_vector(0 to 3);
    signal x, y : std_logic;
    signal lo   : std_logic_vector(1 downto 0);
begin

    clk <= not clk after 5 ns;

    g: for i in 0 to 1 generate
        u_leaf: entity work.leaf
            generic map ( N => 2 )
            port map ( a => q(2 * i + 1 downto 2 * i),
                       b => d(2 * i + 1 downto 2 * i),
                       io => x );
    end generate;

    u_const: entity work.leaf             -- Constant and open
        generic map ( N => 4 )
        port map ( a => "1010", b => open, io => y );

    u_expr: entity work.leaf              -- Expression and ascending range
        generic map ( N => 4 )
        port map ( a => d and "0011", b => e, io => y );

    lo <= q(1 downto 0) and "11";

    p_count: process (clk) is
    begin
        if rising_edge(clk) then
            q(3 downto 2) <= d(1 downto 0);
        end if;
    end process;

end architecture;
