package prot_path1_pkg is
    type holder_t is protected
        impure function p return string;
        impure function i return string;
    end protected;

    type path_only_t is protected
        impure function p return string;
    end protected;
end package;

package body prot_path1_pkg is
    type holder_t is protected body
        variable val : integer := 0;

        impure function p return string is
        begin
            return val'path_name;
        end function;

        impure function i return string is
        begin
            return val'instance_name;
        end function;
    end protected body;

    type path_only_t is protected body
        variable elem : integer := 0;

        impure function p return string is
        begin
            return elem'path_name;
        end function;
    end protected body;
end package body;

use work.prot_path1_pkg.all;

entity prot_path1 is
end entity;

architecture beh of prot_path1 is
    shared variable h : holder_t;
    shared variable po : path_only_t;
begin
    p1: process is
    begin
        assert h.p = ":prot_path1:h:val"
            report "got " & h.p severity failure;
        assert h.i = ":prot_path1(beh):h:val"
            report "got " & h.i severity failure;
        assert po.p = ":prot_path1:po:elem"
            report "got " & po.p severity failure;
        wait;
    end process;
end architecture;
