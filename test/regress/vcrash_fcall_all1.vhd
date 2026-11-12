-- Function call without parameters as the prefix of .all
entity vcrash_fcall_all1 is
end entity;

architecture test of vcrash_fcall_all1 is
    type ptr is access integer;

    function get return ptr is
    begin
        return new integer'(33);
    end function;

    procedure check (x : integer) is
    begin
        assert x = 33;
    end procedure;
begin
    process is
        variable y : integer;
    begin
        y := get.all;
        check(y);
        check(get.all);
        wait;
    end process;
end architecture;
