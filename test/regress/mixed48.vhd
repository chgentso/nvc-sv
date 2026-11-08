entity mixed48_e is end;
architecture a of mixed48_e is
  signal n : integer range 0 to 3 := 2;
  signal neg : integer range -100 to 100 := -42;
  signal wide : integer := 100000;
  signal nat : natural := 7;
begin
  process begin
    wait for 5 ns;
    n <= 3;
    wait for 10 ns;
    neg <= 17; wide <= -200000; nat <= 9;
    wait;
  end process;
end;
