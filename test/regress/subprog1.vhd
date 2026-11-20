entity subprog1_sub is
    generic (
        g : bit_vector(3 downto 0)
    );
end entity;

architecture rtl of subprog1_sub is
begin
    assert g = "1101" severity failure;
end architecture;

entity subprog1 is
end entity;

architecture test of subprog1 is
    procedure check_str(s : string) is
    begin
        assert s'left = 4 and s'right = 8 severity failure;
        assert s = "hello" severity failure;
    end procedure;

    function sum_bv(a : bit_vector(3 downto 0)) return integer is
        variable r : integer := 0;
    begin
        for i in a'range loop
            if a(i) = '1' then
                r := r + 1;
            end if;
        end loop;
        return r;
    end function;

    function check_fn(s : string) return integer is
    begin
        assert s'left = 2 and s'right = 5 severity failure;
        assert s = "abcd" severity failure;
        return s'length;
    end function;
begin
    u1: entity work.subprog1_sub
        generic map (
            g(0) => '1',
            g(3 downto 1) => "110"
        );

    process is
        variable lo : bit_vector(1 downto 0) := "11";
    begin
        check_str(s(4 to 8) => "hello");
        assert check_fn(s(2 to 5) => "abcd") = 4 severity failure;
        assert sum_bv(a(1 downto 0) => lo, a(3 downto 2) => (others => '0')) = 2
            severity failure;
        wait;
    end process;
end architecture;
