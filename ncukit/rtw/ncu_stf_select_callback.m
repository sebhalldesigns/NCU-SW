function ncu_stf_select_callback(hDlg, hSrc)
%NCU_STF_SELECT_CALLBACK Apply NCU code generation defaults.
%
% Invoked from ncu.tlc select/activate callbacks.

    try
        ncu_register_toolchain("UserInstall", true, "SuppressOutput", true);
        i_set_cfg_param(hDlg, hSrc, "UseToolchainInfoCompliant", "on");
        i_set_cfg_param(hDlg, hSrc, "GenCodeOnly", "off");
        i_set_cfg_param(hDlg, hSrc, "GenerateSampleERTMain", "off");
        i_set_cfg_param(hDlg, hSrc, "Toolchain", "NCU STM32 CMake");
        i_set_cfg_param(hDlg, hSrc, "PostCodeGenCommand", "ncu_build_hook");
        i_set_cfg_param(hDlg, hSrc, "ExtMode", "on");
        i_set_cfg_param(hDlg, hSrc, "ExtModeTransport", "0");
        i_set_cfg_param(hDlg, hSrc, "ExtModeMexFile", "ext_xcp");
        i_set_cfg_param(hDlg, hSrc, "ExtModeIntrfLevel", "Level2 - Open");

    catch ME
        warning("NCU:STFSelectCallbackFailed", ...
            "Failed to apply NCU STF defaults: %s", ME.message);
    end
end

function i_set_cfg_param(hDlg, hSrc, paramName, paramValue)
% Prefer config UI API in callback context; fallback to config set.
    try
        slConfigUISetVal(hDlg, hSrc, char(paramName), char(paramValue));
    catch
        cs = [];
        try
            cs = hSrc.getConfigSet();
        catch
        end
        if ~isempty(cs)
            set_param(cs, char(paramName), char(paramValue));
        end
    end
end

