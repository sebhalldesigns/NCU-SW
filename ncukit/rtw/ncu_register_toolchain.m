function tc = ncu_register_toolchain(varargin)
%NCU_REGISTER_TOOLCHAIN Register NCU custom CMake toolchain.
%
% This follows the Embedded Coder custom CMake toolchain approach and
% creates a toolchain entry selectable in Model Settings > Toolchain.
%
% Example:
%   ncu_register_toolchain();
%   ncu_register_toolchain("Force", true);

    p = inputParser;
    addParameter(p, "Force", false, @(x)islogical(x) && isscalar(x));
    addParameter(p, "UserInstall", true, @(x)islogical(x) && isscalar(x));
    addParameter(p, "SuppressOutput", true, @(x)islogical(x) && isscalar(x));
    parse(p, varargin{:});
    opts = p.Results;

    tcName = "NCU STM32 CMake";
    thisDir = fileparts(mfilename("fullpath"));
    repoRoot = fileparts(fileparts(thisDir));
    tcFile = fullfile(thisDir, "ncu_stm32_toolchain.cmake");

    cubeCmakeExe = i_find_cube_cmake_exe();
    wrapperDir = i_ensure_cmake_wrapper(thisDir, cubeCmakeExe);
    armGccBin = i_find_arm_gcc_bin();

    existing = i_get_or_empty("Toolchain", tcName);
    if opts.Force && ~isempty(existing)
        target.remove(existing);
        existing = [];
    end

    if isempty(existing)
        tc = target.create("Toolchain", ...
            Name = tcName, ...
            MakeToolType = "CMake", ...
            Generator = "Ninja", ...
            ToolchainFile = tcFile);

        % Add Windows PATH entries for Ninja and ARM GCC installs.
        tc.EnvironmentConfiguration(1).HostOperatingSystemSupport = target.HostOperatingSystemSupport.All();
        paths = { ...
            wrapperDir, ...
            thisDir, ...
            fileparts(cubeCmakeExe), ...
            "$(MATLAB_ROOT)/toolbox/shared/coder/ninja/$(ARCH)", ...
            "$(MW_MINGW64_LOC)/bin"};
        if strlength(armGccBin) > 0
            paths = [{char(armGccBin)}, paths];
        end
        tc.EnvironmentConfiguration(1).SystemPaths = paths;

        target.add(tc, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
    end

    tc = target.get("Toolchain", tcName);
end

function obj = i_get_or_empty(targetType, objectId)
    try
        obj = target.get(targetType, objectId);
    catch
        obj = [];
    end
end

function cubeCmakeExe = i_find_cube_cmake_exe()
% Find cube-cmake.exe from VS Code extension installation.
    userProfile = getenv("USERPROFILE");
    if strlength(userProfile) == 0
        error("NCU:CubeCMakeNotFound", "USERPROFILE is not set.");
    end

    pattern = fullfile(userProfile, ".vscode", "extensions", ...
        "stmicroelectronics.stm32cube-ide-build-cmake-*", ...
        "resources", "cube-cmake", "win32", "x86_64", "cube-cmake.exe");
    hits = dir(pattern);
    if isempty(hits)
        error("NCU:CubeCMakeNotFound", ...
            "cube-cmake.exe not found under VS Code STM32 extension path.");
    end

    [~, idx] = max([hits.datenum]);
    cubeCmakeExe = fullfile(hits(idx).folder, hits(idx).name);
end

function binDir = i_find_arm_gcc_bin()
% Locate arm-none-eabi-gcc.exe and return containing bin folder.
    binDir = "";

    % 1) If compiler is already on PATH.
    [status, out] = system("where arm-none-eabi-gcc");
    if status == 0
        lines = regexp(strtrim(out), '\r?\n', 'split');
        if ~isempty(lines) && isfile(strtrim(lines{1}))
            binDir = string(fileparts(strtrim(lines{1})));
            return;
        end
    end

    % 2) Typical STM32CubeIDE install roots.
    patterns = { ...
        'C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin\arm-none-eabi-gcc.exe', ...
        'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeIDE_*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin\arm-none-eabi-gcc.exe' ...
    };
    for k = 1:numel(patterns)
        hits = dir(patterns{k});
        if ~isempty(hits)
            [~, idx] = max([hits.datenum]);
            binDir = string(hits(idx).folder);
            return;
        end
    end
end

function wrapperDir = i_ensure_cmake_wrapper(thisDir, cubeCmakeExe)
% Ensure cmake.cmd wrapper exists and delegates to cube-cmake.exe.
    wrapperDir = fullfile(thisDir, "tools");
    if ~isfolder(wrapperDir)
        mkdir(wrapperDir);
    end

    wrapperPath = fullfile(wrapperDir, "cmake.cmd");
    escaped = strrep(cubeCmakeExe, "'", "''");
    lines = [ ...
        "@echo off"
        "setlocal"
        "set ""CUBE_CMAKE=" + escaped + """"
        "if not exist ""%CUBE_CMAKE%"" ("
        "  echo NCU error: cube-cmake.exe not found at %CUBE_CMAKE%"
        "  exit /b 1"
        ")"
        """%CUBE_CMAKE%"" %*"
        "exit /b %ERRORLEVEL%" ...
    ];
    fid = fopen(wrapperPath, "w");
    if fid < 0
        error("NCU:WrapperWriteFailed", "Failed to create %s", wrapperPath);
    end
    cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>
    for i = 1:numel(lines)
        fprintf(fid, "%s\r\n", char(lines(i)));
    end
end
