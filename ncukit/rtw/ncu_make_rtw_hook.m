function ncu_make_rtw_hook(hookMethod, modelName, rtwroot, templateMakefile, buildOpts, buildArgs, buildInfo) %#ok<INUSD>
%NCU_MAKE_RTW_HOOK Build hooks for ncu.tlc target.
% Automatically export ASAP2/A2L after a successful build.

    switch hookMethod
        case 'after_make'
            i_export_a2l(modelName);
        otherwise
            % No action for other phases.
    end
end

function i_export_a2l(modelName)
% Export A2L beside the build artifacts, using ELF symbols when available.

    buildDir = i_get_build_dir(modelName);
    elfPath = fullfile(buildDir, [modelName '.elf']);

    try
        if isfile(elfPath)
            coder.asap2.export(modelName, ...
                'FileName', modelName, ...
                'Folder', buildDir, ...
                'MapFile', elfPath, ...
                'GenerateXCPInfo', true);
        else
            coder.asap2.export(modelName, ...
                'FileName', modelName, ...
                'Folder', buildDir, ...
                'GenerateXCPInfo', true);
            warning('NCU:A2LElfMissing', ...
                ['ELF not found at "%s". Exported A2L without MapFile ', ...
                 'address remap.'], elfPath);
        end
        disp("NCU: A2L export complete.");
    catch ME
        warning('NCU:A2LExportFailed', ...
            'ASAP2 export failed for model "%s": %s', modelName, ME.message);
    end
end

function buildDir = i_get_build_dir(modelName)
% Resolve build directory in a way that works for generated standalone builds.

    buildRoot = '';
    try
        b = RTW.getBuildDir(modelName);
        buildRoot = b.BuildDirectory;
    catch
    end

    % Prefer the directory where the generated HEX/ELF actually lives.
    if ~isempty(buildRoot) && isfolder(buildRoot)
        hexHits = dir(fullfile(buildRoot, '**', [modelName '.hex']));
        if ~isempty(hexHits)
            buildDir = hexHits(1).folder;
            return;
        end

        elfHits = dir(fullfile(buildRoot, '**', [modelName '.elf']));
        if ~isempty(elfHits)
            buildDir = elfHits(1).folder;
            return;
        end
    end

    candidates = {};

    try
        buildInfo = coder.internal.ModelCodegenMgr.getInstance(modelName).BuildInfo;
        candidates{end+1} = buildInfo.Settings.LocalAnchorDir; %#ok<AGROW>
        candidates{end+1} = fullfile(buildInfo.Settings.LocalAnchorDir, 'build'); %#ok<AGROW>
    catch
    end

    if ~isempty(buildRoot)
        candidates{end+1} = buildRoot; %#ok<AGROW>
        candidates{end+1} = fullfile(buildRoot, 'build'); %#ok<AGROW>
    end

    candidates{end+1} = pwd;
    candidates{end+1} = fullfile(pwd, 'build');
    candidates{end+1} = fullfile(pwd, [modelName '_ncu_rtw']);
    candidates{end+1} = fullfile(pwd, [modelName '_ncu_rtw'], 'build');

    buildDir = '';
    candidates = unique(candidates);

    for k = 1:numel(candidates)
        d = candidates{k};
        if isempty(d) || ~isfolder(d)
            continue;
        end
        if isfile(fullfile(d, [modelName '.elf']))
            buildDir = d;
            return;
        end
    end

    for k = 1:numel(candidates)
        d = candidates{k};
        if ~isempty(d) && isfolder(d)
            buildDir = d;
            return;
        end
    end

    buildDir = pwd;
end
