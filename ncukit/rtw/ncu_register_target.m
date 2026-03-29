function addedObjects = ncu_register_target(varargin)
%NCU_REGISTER_TARGET Register NCU processor/board for Hardware Board list.
%
% Example:
%   ncu_register_target();
%   ncu_register_target("Force", true);
%
% This function is safe to call repeatedly. It only adds objects if missing.

    p = inputParser;
    addParameter(p, "Force", false, @(x)islogical(x) && isscalar(x));
    addParameter(p, "UserInstall", true, @(x)islogical(x) && isscalar(x));
    addParameter(p, "SuppressOutput", true, @(x)islogical(x) && isscalar(x));
    parse(p, varargin{:});
    opts = p.Results;

    processorManufacturer = "NCU";
    processorName = "ARM Compatible->ARM Cortex-M";
    processorId = processorManufacturer + "-" + processorName;
    boardName = "NCU";
    baseLangImplId = "ARM Compatible-ARM Cortex";

    existingProcessor = i_get_or_empty("Processor", processorId);
    existingBoard = i_get_or_empty("Board", boardName);

    if opts.Force
        if ~isempty(existingBoard)
            target.remove(existingBoard);
            existingBoard = [];
        end
        if ~isempty(existingProcessor)
            target.remove(existingProcessor);
            existingProcessor = [];
        end
    end

    if isempty(existingProcessor)
        langImpl = target.get("LanguageImplementation", baseLangImplId);
        proc = target.create("Processor", ...
            Name = processorName, ...
            Manufacturer = processorManufacturer);
        proc.LanguageImplementations = langImpl;
        addedProcessor = target.add(proc, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        existingProcessor = addedProcessor(1);
    end

    if isempty(existingBoard)
        board = target.create("Board", Name = boardName);
        board.Processors = existingProcessor;
        target.add(board, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
    end

    addedObjects = target.get("Board", boardName);
end

function obj = i_get_or_empty(targetType, objectId)
% Return [] when object is not registered.
    try
        obj = target.get(targetType, objectId);
    catch
        obj = [];
    end
end
