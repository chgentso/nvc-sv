-- A VHPI value change callback on a slice of a signal that is driven by a
-- Verilog module splits the nexus; an update that was still queued must
-- also be performed for the new part.  See test/vhpi/vhpi32.c.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ram is
    port (clk : in std_logic;
          ab  : in std_logic_vector(9 downto 0);
          q   : out std_logic_vector(31 downto 0));
end entity;

architecture test of ram is
    type t_ram is array (0 to 1023) of std_logic_vector(31 downto 0);
    shared variable mem : t_ram := (others => (others => '0'));
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            q <= mem(to_integer(unsigned(ab)));
            mem(to_integer(unsigned(ab))) := std_logic_vector(resize(unsigned(ab), 32) + 100);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity ram_wrap is
    port (clk : in std_logic;
          ab  : in std_logic_vector(9 downto 0);
          q   : out std_logic_vector(31 downto 0));
end entity;

architecture test of ram_wrap is
begin
    u_ram : entity work.ram port map (clk, ab, q);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library ieee;
use ieee.std_logic_1164.all;

entity tap is
    port (clk : in std_logic;
          ab  : in std_logic_vector(5 downto 0);
          cnt : out natural);
end entity;

architecture test of tap is
begin
    process (ab) is
        variable n : natural := 0;
    begin
        n := n + 1;
        cnt <= n;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhpi32 is
end entity;

architecture test of vhpi32 is
    signal clk  : std_logic := '0';
    signal addr : std_logic_vector(31 downto 0);
    signal q    : std_logic_vector(31 downto 0);
    signal sum  : natural := 0;
    signal taps : natural := 0;

    component vhpi_cpu is
        port (clk  : in std_logic;
              addr : out std_logic_vector(31 downto 0));
    end component;
begin
    clk <= not clk after 5 ns when now < 1 us;

    u_cpu : component vhpi_cpu port map (clk, addr);
    u_ram : entity work.ram_wrap port map (clk, addr(11 downto 2), q);

    u_tap : entity work.tap port map (clk, addr(7 downto 2), taps);

    process (clk) is
    begin
        if rising_edge(clk) and not is_x(q) then
            sum <= sum + to_integer(unsigned(q));
        end if;
    end process;

    process is
    begin
        wait for 1 us;
        report "sum = " & integer'image(sum) & " taps = " & integer'image(taps);
        assert sum = 8903 report "wrong sum" severity failure;
        wait;
    end process;
end architecture;
