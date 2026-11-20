package attr25_pkg is
    component pkg_comp is
    end component;
    type pkg_type is (a, b);
end package;

use work.attr25_pkg.all;

entity attr25 is
    type ent_type is range 0 to 7;
end entity;

architecture test of attr25 is
    component my_comp is
    end component;
    type arch_type is (x, y);
    subtype arch_sub is natural range 0 to 3;
begin
    p1: process is
        type proc_type is range 1 to 4;
        type int_file is file of integer;
        file f : int_file;
    begin
        assert my_comp'path_name = ":attr25:my_comp"
            report my_comp'path_name severity failure;
        assert my_comp'instance_name = ":attr25(test):my_comp"
            report my_comp'instance_name severity failure;
        assert pkg_comp'path_name = ":work:attr25_pkg:pkg_comp"
            report pkg_comp'path_name severity failure;
        assert pkg_type'path_name = ":work:attr25_pkg:pkg_type"
            report pkg_type'path_name severity failure;
        assert ent_type'path_name = ":attr25:ent_type"
            report ent_type'path_name severity failure;
        assert arch_type'path_name = ":attr25:arch_type"
            report arch_type'path_name severity failure;
        assert arch_sub'instance_name = ":attr25(test):arch_sub"
            report arch_sub'instance_name severity failure;
        assert proc_type'path_name = ":attr25:p1:proc_type"
            report proc_type'path_name severity failure;
        assert f'path_name = ":attr25:p1:f"
            report f'path_name severity failure;
        wait;
    end process;
end architecture;
