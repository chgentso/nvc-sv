-- process(all) calling a same-unit impure function that reads a signal and its
-- implicit S'STABLE signal (LRM 11.3).

entity implicit13 is
end entity;

architecture test of implicit13 is
    signal s : bit := '0';
    signal count : integer := 0;

    impure function check_sig return integer is
    begin
        if s = '0' and s'stable then
            return 1;
        elsif s = '1' and not s'stable then
            return 2;
        elsif s = '1' and s'stable then
            return 3;
        else
            return 99;
        end if;
    end function;
begin
    mon : process (all) is
        variable step : integer := 0;
    begin
        step := step + 1;
        if step = 1 then
            assert check_sig = 1;
        elsif step = 2 then
            assert check_sig = 2;
        elsif step = 3 then
            assert check_sig = 3;
        end if;
        count <= step;
    end process;

    stim : process is
    begin
        wait for 1 ns;
        s <= '1';
        wait for 1 ns;
        assert count = 3 report integer'image(count);
        wait;
    end process;
end architecture;
