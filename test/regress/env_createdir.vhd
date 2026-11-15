use std.env.all;

entity env_createdir is
end entity;

architecture test of env_createdir is
    constant root : string := "env_createdir_tmp";
begin
    process is
        variable s1, s2, s3 : dir_create_status;
        variable ds : dir_delete_status;
    begin
        dir_createdir(root & "/p/q", false, s1);     -- Missing parent
        assert s1 = STATUS_ERROR;
        dir_createdir(root, false, s1);
        assert s1 = STATUS_OK;
        dir_createdir(root & "/p/q/r", true, s2);
        assert s2 = STATUS_OK report to_string(s2);
        assert dir_itemexists(root & "/p/q/r");
        dir_createdir(root & "/p/q/r", true, s3);
        assert s3 = STATUS_ITEM_EXISTS;
        ds := dir_deletedir(root, true);
        assert ds = STATUS_OK and not dir_itemexists(root);
        wait;
    end process;
end architecture;
