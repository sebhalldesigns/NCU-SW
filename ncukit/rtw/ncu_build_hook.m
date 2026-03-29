function ncu_build_hook()

    disp("Invoking NCU build hook...");

    modelName = bdroot;
    thisFile = mfilename('fullpath');
    thisDir  = fileparts(thisFile);

    % Get build info (official way via coder API)
    buildInfo = coder.internal.ModelCodegenMgr.getInstance(modelName).BuildInfo;

    buildInfo.Src.Files.FileName

    index = find(strcmp({buildInfo.Src.Files.FileName}, 'rt_main.c'))
    if (length(index) > 0)
        buildInfo.Src.Files(index) = [];
        buildInfo.addSourceFiles('main.c', fullfile(thisDir, '../src'));
    end

end