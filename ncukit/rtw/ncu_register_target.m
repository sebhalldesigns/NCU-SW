function addedObjects = ncu_register_target(varargin)
%NCU_REGISTER_TARGET Register NCU processor/board for Hardware Board list.
%
% Example:
%   ncu_register_target();
%   ncu_register_target("Force", true);
%
% This function is safe to call repeatedly. It only adds objects if missing.
% It registers an XCP/TCP external mode stack plus matching TCP connection
% so Simulink exposes Monitor & Tune connectivity for the NCU board.

    p = inputParser;
    addParameter(p, "Force", false, @(x)islogical(x) && isscalar(x));
    addParameter(p, "UserInstall", true, @(x)islogical(x) && isscalar(x));
    addParameter(p, "SuppressOutput", true, @(x)islogical(x) && isscalar(x));
    addParameter(p, "IPAddress", "192.168.1.50", @i_is_text_scalar);
    addParameter(p, "Port", "5005", @i_is_valid_port);
    addParameter(p, "RegisterExternalMode", true, @(x)islogical(x) && isscalar(x));
    parse(p, varargin{:});
    opts = p.Results;

    opts.IPAddress = string(opts.IPAddress);
    opts.Port = string(i_port_as_number(opts.Port));

    processorManufacturer = "NCU";
    processorName = "ARM Compatible->ARM Cortex-M";
    processorId = processorManufacturer + "-" + processorName;
    boardName = "NCU";
    baseLangImplId = "ARM Compatible-ARM Cortex";
    externalModeName = "NCU External Mode";
    communicationInterfaceName = "NCU TCP Interface";
    executionToolName = "NCU Execution Service";
    executionToolImplementationName = "NCU Execution Tool Implementation";
    tcpConnectionName = "NCU XCP TCP Connection";

    existingProcessor = i_get_or_empty("Processor", processorId);
    existingBoard = i_get_or_empty("Board", boardName);
    existingExternalMode = i_get_or_empty("ExternalMode", externalModeName);
    existingCommunicationInterface = i_get_or_empty("CommunicationInterface", communicationInterfaceName);
    existingExecutionToolImplementation = i_get_or_empty("APIImplementation", executionToolImplementationName);
    existingExecutionTool = i_get_or_empty("ExecutionService", executionToolName);
    existingTCPConnection = i_get_or_empty("TargetConnection", tcpConnectionName);

    if isempty(existingProcessor)
        langImpl = target.get("LanguageImplementation", baseLangImplId);
        proc = target.create("Processor", ...
            Name = processorName, ...
            Manufacturer = processorManufacturer);
        proc.LanguageImplementations = langImpl;
        addedProcessor = target.add(proc, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        if isempty(addedProcessor)
            existingProcessor = i_get_or_empty("Processor", processorId);
        else
            existingProcessor = addedProcessor(1);
        end
    elseif opts.Force
        langImpl = target.get("LanguageImplementation", baseLangImplId);
        existingProcessor.Manufacturer = processorManufacturer;
        existingProcessor.LanguageImplementations = langImpl;
        target.update(existingProcessor);
    end

    if isempty(existingBoard)
        board = target.create("Board", Name = boardName);
        board.Processors = existingProcessor;
        addedBoard = target.add(board, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        if isempty(addedBoard)
            existingBoard = i_get_or_empty("Board", boardName);
        else
            existingBoard = addedBoard(1);
        end
    elseif opts.Force
        existingBoard.Processors = existingProcessor;
        target.update(existingBoard);
    end

    if opts.RegisterExternalMode
        if isempty(existingExternalMode)
            externalMode = i_create_external_mode_stack(externalModeName);
            addedExternalMode = target.add(externalMode, ...
                UserInstall = opts.UserInstall, ...
                SuppressOutput = opts.SuppressOutput);
            if isempty(addedExternalMode)
                existingExternalMode = i_get_or_empty("ExternalMode", externalModeName);
            else
                existingExternalMode = addedExternalMode(1);
            end
        elseif opts.Force
            existingExternalMode.Connectivities = i_create_external_mode_connectivity();
            target.update(existingExternalMode);
        end
    end

    if i_board_external_mode_needs_refresh(existingBoard, existingExternalMode, opts.RegisterExternalMode)
        existingBoard.CommunicationProtocolStacks = existingExternalMode;
        target.update(existingBoard);
    end

    if isempty(existingCommunicationInterface)
        communicationInterface = i_create_tcp_communication_interface();
        addedCommunicationInterface = target.add(communicationInterface, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        if isempty(addedCommunicationInterface)
            existingCommunicationInterface = i_get_or_empty("CommunicationInterface", communicationInterfaceName);
        else
            existingCommunicationInterface = addedCommunicationInterface(1);
        end
    elseif opts.Force
        existingCommunicationInterface.APIImplementations = i_create_tcp_api_implementation();
        target.update(existingCommunicationInterface);
    end

    if i_board_communication_interface_needs_refresh(existingBoard, existingCommunicationInterface)
        existingBoard.CommunicationInterfaces = existingCommunicationInterface;
        target.update(existingBoard);
    end

    if isempty(existingExecutionToolImplementation)
        executionToolImplementation = i_create_execution_tool_api_implementation(executionToolImplementationName);
        addedExecutionToolImplementation = target.add(executionToolImplementation, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        if isempty(addedExecutionToolImplementation)
            existingExecutionToolImplementation = i_get_or_empty("APIImplementation", executionToolImplementationName);
        else
            existingExecutionToolImplementation = addedExecutionToolImplementation(1);
        end
    elseif opts.Force
        existingExecutionToolImplementation.API = target.get("API", "ExecutionTool");
        existingExecutionToolImplementation.BuildDependencies = i_create_execution_tool_matlab_dependencies();
        target.update(existingExecutionToolImplementation);
    end

    if isempty(existingExecutionTool)
        executionTool = i_create_execution_service(executionToolName, existingExecutionToolImplementation);
        addedExecutionTool = target.add(executionTool, ...
            UserInstall = opts.UserInstall, ...
            SuppressOutput = opts.SuppressOutput);
        if isempty(addedExecutionTool)
            existingExecutionTool = i_get_or_empty("ExecutionService", executionToolName);
        else
            existingExecutionTool = addedExecutionTool(1);
        end
    elseif opts.Force
        existingExecutionTool.APIImplementation = existingExecutionToolImplementation;
        existingExecutionTool.HostOperatingSystemSupport = target.HostOperatingSystemSupport.WindowsOnly();
        target.update(existingExecutionTool);
    end

    if i_board_execution_tool_needs_refresh(existingBoard, existingExecutionTool)
        existingBoard.Tools.ExecutionTools = existingExecutionTool;
        target.update(existingBoard);
    end

    if opts.RegisterExternalMode && ...
            i_connection_needs_refresh(existingTCPConnection, existingBoard, "TCPChannel", opts.IPAddress, opts.Port)
        if ~isempty(existingTCPConnection)
            existingTCPConnection.Target = existingBoard;
            existingTCPConnection.CommunicationChannel.IPAddress = char(opts.IPAddress);
            existingTCPConnection.CommunicationChannel.Port = char(opts.Port);
            target.update(existingTCPConnection);
        else
            tcpConnection = target.create("TargetConnection", ...
                Name = tcpConnectionName, ...
                Target = existingBoard, ...
                CommunicationType = "TCPChannel", ...
                IPAddress = char(opts.IPAddress), ...
                Port = char(opts.Port));
            target.add(tcpConnection, ...
                UserInstall = opts.UserInstall, ...
                SuppressOutput = opts.SuppressOutput);
        end
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

function tf = i_connection_needs_refresh(connection, board, channelType, ipAddress, port)
% Recreate the connection when it is missing or has stale settings.
    if isempty(connection) || isempty(board)
        tf = true;
        return;
    end

    tf = true;
    try
        sameBoard = strcmp(string(connection.Target.Name), string(board.Name));
        sameChannel = isa(connection.CommunicationChannel, "target." + channelType);
        sameIP = strcmp(string(connection.CommunicationChannel.IPAddress), string(ipAddress));
        samePort = strcmp(string(connection.CommunicationChannel.Port), string(port));
        tf = ~(sameBoard && sameChannel && sameIP && samePort);
    catch
        tf = true;
    end
end

function tf = i_board_external_mode_needs_refresh(board, externalMode, registerExternalMode)
% Recreate the board external mode stack when it is missing or stale.
    if isempty(board) || isempty(externalMode) || ~registerExternalMode
        tf = false;
        return;
    end

    tf = true;
    try
        stacks = board.CommunicationProtocolStacks;
        if isempty(stacks)
            return;
        end

        extMode = stacks(1);
        tf = ~strcmp(string(extMode.Name), string(externalMode.Name));
    catch
        tf = true;
    end
end

function tf = i_board_communication_interface_needs_refresh(board, communicationInterface)
% Ensure the board exposes a TCP communication interface for Simulink.
    if isempty(board) || isempty(communicationInterface)
        tf = false;
        return;
    end

    tf = true;
    try
        interfaces = board.CommunicationInterfaces;
        if isempty(interfaces)
            return;
        end

        iface = interfaces(1);
        tf = ~strcmp(string(iface.Name), string(communicationInterface.Name));
    catch
        tf = true;
    end
end

function tf = i_board_execution_tool_needs_refresh(board, executionTool)
% Ensure the board has an execution tool so the hardware UI is populated.
    if isempty(board) || isempty(executionTool)
        tf = false;
        return;
    end

    tf = true;
    try
        executionTools = board.Tools.ExecutionTools;
        if isempty(executionTools)
            return;
        end

        boardExecutionTool = executionTools(1);
        tf = ~strcmp(string(boardExecutionTool.Name), string(executionTool.Name));
    catch
        tf = true;
    end
end

function externalMode = i_create_external_mode_stack(externalModeName)
% Register an XCP/TCP external mode stack so Simulink exposes Monitor & Tune.
    externalMode = target.create("ExternalMode", ...
        Name = externalModeName, ...
        Connectivities = i_create_external_mode_connectivity());
end

function extModeConnectivity = i_create_external_mode_connectivity()
    xcpTransport = target.create("XCPTCPIPTransport", ...
        Name = "NCU XCP TCP Transport");
    xcpTransport.MaxCTOSize = uint8(8);
    xcpTransport.MaxDTOSize = uint16(8);
    xcpTransport.MaxODTEntrySizeDAQ = uint8(7);

    xcpConfiguration = target.create("XCP", ...
        Name = "NCU XCP TCP Configuration", ...
        XCPTransport = xcpTransport);

    extModeConnectivity = target.create("XCPExternalModeConnectivity", ...
        Name = "NCU External Mode TCP Connectivity", ...
        XCP = xcpConfiguration);
end

function tcpComms = i_create_tcp_communication_interface()
% Create TCP communication interface metadata for the board.
    tcpComms = target.create("CommunicationInterface");
    tcpComms.Name = "NCU TCP Interface";
    tcpComms.Channel = "TCPChannel";
    tcpComms.APIImplementations = i_create_tcp_api_implementation();
end

function apiImplementation = i_create_tcp_api_implementation()
    api = target.get("API", "rtiostream");

    buildDependencies = target.create("BuildDependencies");
    buildDependencies.SourceFiles = {fullfile("$(MATLAB_ROOT)", ...
                                              "toolbox", ...
                                              "coder", ...
                                              "rtiostream", ...
                                              "src", ...
                                              "rtiostreamtcpip", ...
                                              "rtiostream_tcpip.c")};
    buildDependencies.IncludePaths = {fullfile("$(MATLAB_ROOT)", ...
                                               "toolbox", ...
                                               "coder", ...
                                               "rtiostream", ...
                                               "src", ...
                                               "rtiostreamtcpip")};

    apiImplementation = target.create("APIImplementation", ...
        Name = "NCU TCP rtIOStream Implementation");
    apiImplementation.API = api;
    apiImplementation.BuildDependencies = buildDependencies;
end

function executionToolImplementation = i_create_execution_tool_api_implementation(executionToolImplementationName)
% Register MATLAB-backed execution tool implementation for deploy/start/status.
    executionToolImplementation = target.create("APIImplementation", ...
        Name = executionToolImplementationName);
    executionToolImplementation.API = target.get("API", "ExecutionTool");
    executionToolImplementation.BuildDependencies = i_create_execution_tool_matlab_dependencies();
end

function matlabDependencies = i_create_execution_tool_matlab_dependencies()
    matlabDependencies = target.create("MATLABDependencies");
    matlabDependencies.Classes = "ncu.NCUExecutionTool";
end

function executionTool = i_create_execution_service(executionToolName, executionToolImplementation)
% Register execution service so Build/Deploy/Start and status use MATLAB code.
    executionTool = target.create("ExecutionService", ...
        Name = executionToolName);
    executionTool.APIImplementation = executionToolImplementation;
    executionTool.HostOperatingSystemSupport = target.HostOperatingSystemSupport.WindowsOnly();
end

function tf = i_is_text_scalar(value)
    tf = (ischar(value) && isrow(value)) || (isstring(value) && isscalar(value));
end

function tf = i_is_valid_port(value)
    try
        port = i_port_as_number(value);
        tf = isfinite(port) && port >= 0 && port <= 65535 && floor(port) == port;
    catch
        tf = false;
    end
end

function port = i_port_as_number(value)
    if isnumeric(value)
        port = double(value);
        return;
    end

    value = string(value);
    if ~isscalar(value)
        error("NCU:InvalidPort", "Port must be a scalar value.");
    end

    port = str2double(value);
    if isnan(port)
        error("NCU:InvalidPort", "Port must be numeric.");
    end
end
