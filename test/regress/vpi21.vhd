-- VHDL top level of test vpi21: drivers of signals in a design with VHDL
-- processes, a resolved signal, ports that nvc merges with the signal they
-- are connected to, output ports, and a Verilog instance.  The C side is
-- test/vpi/vpi21.c, it asks vpi_nvc_connections at the start of the
-- simulation.

library ieee;
use ieee.std_logic_1164.all;

entity vpi21_leaf is
    generic ( n : natural := 2 );
    port ( a : in  std_logic_vector(n - 1 downto 0);
           b : out std_logic_vector(n - 1 downto 0) );
end entity;

architecture rtl of vpi21_leaf is
    signal tmp : std_logic_vector(n - 1 downto 0);
begin
    tmp <= not a;
    p_out: process (tmp) is
    begin
        b <= tmp;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vpi21 is
end entity;

architecture test of vpi21 is
    component vpi21_sub is
        generic ( w : integer := 4 );
        port ( clk : in  std_logic;
               d   : in  std_logic_vector(w - 1 downto 0);
               q   : out std_logic_vector(w - 1 downto 0) );
    end component;

    signal clk  : std_logic := '0';
    signal q    : std_logic_vector(3 downto 0) := "0000";
    signal d    : std_logic_vector(3 downto 0) := "0101";
    signal vq   : std_logic_vector(3 downto 0);
    signal bus1 : std_logic;           -- resolved, two drivers
    signal w    : std_logic_vector(1 downto 0);
begin
    clk <= not clk after 5 ns;

    p_lo: process (clk) is
    begin
        q(1 downto 0) <= d(1 downto 0);
    end process;

    q(3 downto 2) <= d(3 downto 2);          -- concurrent assignment

    p_a: process is
    begin
        bus1 <= '1';
        wait;
    end process;

    p_b: process is
    begin
        bus1 <= 'Z';
        wait;
    end process;

    u_leaf: entity work.vpi21_leaf
        generic map ( n => 2 )
        port map ( a => q(3 downto 2), b => w );

    u_sub: vpi21_sub port map ( clk => clk, d => d, q => vq );
end architecture;
