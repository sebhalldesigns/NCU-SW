function sl_customization(cm)
% NCU Simulink customization hook.
%
% R2023b does not support legacy RTW.TargetInfo registration.
% Register hardware board data with the target.* framework instead.
    %#ok<INUSD>

    try
        ncu_register_target("UserInstall", true, "SuppressOutput", true);
    catch ME
        warning("NCU:TargetRegistrationFailed", ...
            "NCU board registration skipped: %s", ME.message);
    end

    try
        ncu_register_toolchain("UserInstall", true, "SuppressOutput", true);
    catch ME
        warning("NCU:ToolchainRegistrationFailed", ...
            "NCU toolchain registration skipped: %s", ME.message);
    end

end
