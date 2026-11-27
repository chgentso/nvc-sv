-- Zero-delay assignments that do not change the driver value in code
-- compiled by LLVM: the driver is still active in the next delta cycle,
-- 'transaction toggles and 'last_active is updated, for signals of
-- different sizes, also when the value is first changed and then
-- assigned again in the same delta cycle
library ieee;
use ieee.std_logic_1164.all;

entity signal41 is
end entity;

architecture test of signal41 is
    signal b8   : bit_vector(7 downto 0) := X"5a";
    signal v16  : std_logic_vector(15 downto 0) := X"1234";
    signal v100 : std_logic_vector(99 downto 0) := (others => 'H');
    signal v200 : std_logic_vector(199 downto 0) := (others => 'L');
    signal sl   : std_logic := 'Z';
    signal int  : integer := -7;
    signal re   : real := 1.5;
    signal sp   : bit_vector(7 downto 0) := X"00";   -- Split by a reader
    signal step : natural := 0;
    signal go   : natural := 0;

    function act (s : boolean) return character is
    begin
        if s then return 'A'; else return '-'; end if;
    end function;
begin

    driver: process is
    begin
        for i in 1 to 4 loop
            wait for 1 ns;
            b8   <= X"5a";
            v16  <= X"1234";
            v100 <= (others => 'H');
            v200 <= (others => 'L');
            sl   <= 'Z';
            int  <= -7;
            re   <= 1.5;
            sp   <= X"00";
            if i = 2 then
                v16 <= X"4321";                 -- Changes then same again
                v16 <= X"4321";
            elsif i = 3 then
                v16 <= X"1234";                 -- Back to the old value
                int <= 8;
            end if;
            step <= i;
            if i = 1 then
                -- Assigns again in the delta cycle where the transactions
                -- above are applied, and reads 'active straight after
                wait for 0 ns;
                b8 <= X"5a";
                report "again b8'active=" & boolean'image(b8'active)
                    & " sl'active=" & boolean'image(sl'active);
                wait for 0 ns;
                report "after b8'active=" & boolean'image(b8'active)
                    & " sl'active=" & boolean'image(sl'active);
            end if;
        end loop;
        wait;
    end process;

    low: process (sp(1 downto 0)) is
    begin
        go <= go + 1;
    end process;

    -- 'transaction readers make the driver take the full path
    tx: process (b8'transaction, sp'transaction) is
    begin
        report "tx " & act(b8'active) & act(sp'active);
    end process;

    act_mon: process is
    begin
        wait on step, go;
        report "act " & act(b8'active) & act(v16'active) & act(v100'active)
            & act(v200'active) & act(sl'active) & act(int'active)
            & act(re'active) & act(sp'active)
            & " v16=" & to_hstring(v16) & " int=" & integer'image(int)
            & " ev=" & act(v16'event) & act(int'event);
    end process;

    check: process (step) is
    begin
        if step > 0 then
            report "step " & integer'image(step)
                & " b8'last_active=" & time'image(b8'last_active)
                & " v200'last_active=" & time'image(v200'last_active)
                & " re'last_active=" & time'image(re'last_active)
                & " v100'last_active=" & time'image(v100'last_active)
                & " int'last_active=" & time'image(int'last_active);
        end if;
    end process;

    final: process is
    begin
        wait for 10 ns;
        report "final b8'last_active=" & time'image(b8'last_active)
            & " v16'last_active=" & time'image(v16'last_active)
            & " v100'last_active=" & time'image(v100'last_active)
            & " v200'last_active=" & time'image(v200'last_active)
            & " sl'last_active=" & time'image(sl'last_active)
            & " int'last_active=" & time'image(int'last_active)
            & " re'last_active=" & time'image(re'last_active)
            & " sp'last_active=" & time'image(sp'last_active);
        wait;
    end process;

end architecture;
