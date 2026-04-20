classdef NCUExecutionTool < target.ExecutionTool
    properties (Access = private)
        StandardOutput string = ""
        StandardError string = ""
        LoadedApplicationPath string = ""
        LoadedApplicationSegments struct = struct('Address', {}, 'Data', {})
    end

    methods
        function errFlag = open(~)
            errFlag = false;
        end

        function errFlag = close(~)
            errFlag = false;
        end

        function errFlag = loadApplication(this)
            errFlag = false;

            try
                [applicationPath, imageType] = this.i_resolve_application_path();
                [segments, totalBytes] = this.i_load_application_segments(applicationPath, imageType);

                this.LoadedApplicationPath = applicationPath;
                this.LoadedApplicationSegments = segments;
                this.i_append_output(sprintf( ...
                    'Loaded %u byte(s) across %u segment(s) from %s for XCP programming.', ...
                    totalBytes, numel(segments), char(applicationPath)));
            catch ME
                errFlag = true;
                this.StandardError = this.i_append_message(this.StandardError, string(ME.message));
            end
        end

        function errFlag = unloadApplication(this)
            this.LoadedApplicationPath = "";
            this.LoadedApplicationSegments = struct('Address', {}, 'Data', {});
            errFlag = false;
        end

        function errFlag = startApplication(this)
            errFlag = false;
            this.StandardOutput = "";
            this.StandardError = "";

            if isempty(this.LoadedApplicationSegments)
                errFlag = this.loadApplication();
                if errFlag
                    return;
                end
            end

            [host, port, ok] = this.i_get_connection_details();
            if ~ok
                this.StandardError = this.i_append_message(this.StandardError, ...
                    "Connection details are unavailable.");
                errFlag = true;
                return;
            end

            try
                client = this.i_open_xcp_client(host, port);
                cleanupObj = onCleanup(@() this.i_close_xcp_client(client)); %#ok<NASGU>

                [client, connectInfo] = this.i_xcp_connect(client);
                this.i_append_output(sprintf( ...
                    'XCP CONNECT succeeded. CAL=%d DAQ=%d STIM=%d PGM=%d.', ...
                    connectInfo.Resource.CAL, ...
                    connectInfo.Resource.DAQ, ...
                    connectInfo.Resource.STIM, ...
                    connectInfo.Resource.PGM));

                if ~connectInfo.Resource.PGM
                    error("NCU:XCPNoPGM", ...
                        "Target XCP server does not advertise the PGM resource. Enable XCP programming support in firmware first.");
                end

                if connectInfo.Protection.PGM
                    error("NCU:XCPPGMLocked", ...
                        "Target XCP server reports the PGM resource as locked. Seed/key unlock is not implemented on the MATLAB side yet.");
                end

                [client, pgmInfo] = this.i_xcp_get_pgm_processor_info(client, connectInfo);
                if ~pgmInfo.AbsoluteMode
                    error("NCU:XCPUnsupportedPGMMode", ...
                        "The MATLAB XCP programmer currently supports only absolute programming mode.");
                end

                [client, pgmSession] = this.i_xcp_program_start(client, connectInfo);
                client = this.i_program_segments(client, connectInfo, pgmSession, this.LoadedApplicationSegments);
                this.i_xcp_program_reset(client);

                this.i_append_output("XCP programming sequence completed.");
            catch ME
                errFlag = true;
                this.StandardError = this.i_append_message(this.StandardError, string(ME.message));
            end
        end

        function errFlag = stopApplication(this)
            [host, port, ok] = this.i_get_connection_details();
            if ~ok
                this.i_append_output("Stop requested. No XCP disconnect was attempted because the connection details are unavailable.");
                errFlag = false;
                return;
            end

            try
                client = this.i_open_xcp_client(host, port);
                cleanupObj = onCleanup(@() this.i_close_xcp_client(client)); %#ok<NASGU>
                [client, ~] = this.i_xcp_connect(client);
                this.i_xcp_disconnect(client);
                this.i_append_output("Issued XCP disconnect.");
                errFlag = false;
            catch ME
                this.StandardError = this.i_append_message(this.StandardError, ...
                    "Stop requested but XCP disconnect failed: " + string(ME.message));
                errFlag = false;
            end
        end

        function [applicationStatus, errFlag] = getApplicationStatus(this)
            errFlag = false;
            applicationStatus = target.ApplicationStatus.Unknown;

            [host, port, ok] = this.i_get_connection_details();
            if ~ok
                errFlag = true;
                this.StandardError = this.i_append_message(this.StandardError, ...
                    "Connection details are unavailable.");
                return;
            end

            [isRunning, usedVnt, message] = this.i_probe_xcp_server(host, port);
            if isRunning
                applicationStatus = target.ApplicationStatus.Running;
                if strlength(message) > 0
                    this.i_append_output(message);
                end
                return;
            end

            applicationStatus = target.ApplicationStatus.Stopped;
            if strlength(message) > 0
                this.StandardError = this.i_append_message(this.StandardError, message);
            elseif usedVnt
                this.StandardError = this.i_append_message(this.StandardError, ...
                    "Vehicle Network Toolbox XCP probe failed.");
            end
        end

        function [stdOutputStream, errFlag] = getStandardOutput(this)
            stdOutputStream = this.StandardOutput;
            errFlag = false;
        end

        function [stdErrorStream, errFlag] = getStandardError(this)
            stdErrorStream = this.StandardError;
            errFlag = false;
        end
    end

    methods (Access = private)
        function [host, port, ok] = i_get_connection_details(this)
            host = "";
            port = 0;
            ok = false;

            try
                channel = this.Connection.CommunicationChannel;
                host = string(channel.IPAddress);
                port = str2double(string(channel.Port));
                ok = (strlength(host) > 0) && isfinite(port) && (port > 0);
            catch
                ok = false;
            end
        end

        function [applicationPath, imageType] = i_resolve_application_path(this)
            applicationPath = string(this.Application);
            imageType = "";

            if strlength(applicationPath) == 0
                error("NCU:MissingApplication", ...
                    "Application path is empty.");
            end

            if ~isfile(applicationPath)
                candidate = string(fullfile(pwd, applicationPath));
                if isfile(candidate)
                    applicationPath = candidate;
                end
            end

            if ~isfile(applicationPath)
                error("NCU:ApplicationNotFound", ...
                    "Application '%s' was not found.", applicationPath);
            end

            [folder, base, ext] = fileparts(applicationPath);
            ext = lower(string(ext));

            if ext == ".hex"
                imageType = "hex";
                return;
            end

            hexCandidate = string(fullfile(folder, base + ".hex"));
            if isfile(hexCandidate)
                applicationPath = hexCandidate;
                imageType = "hex";
                return;
            end

            if ext == ".bin"
                error("NCU:BinaryImageUnsupported", ...
                    "Binary image '%s' has no load address information. Use a HEX image or generate a companion XCP programming manifest.", applicationPath);
            end

            error("NCU:ImageFormatUnsupported", ...
                "XCP programming on the MATLAB side currently requires a HEX image. No HEX companion was found for '%s'.", applicationPath);
        end

        function [segments, totalBytes] = i_load_application_segments(this, applicationPath, imageType)
            switch imageType
                case "hex"
                    segments = this.i_parse_intel_hex(applicationPath);
                otherwise
                    error("NCU:UnsupportedImageType", ...
                        "Unsupported application image type '%s'.", imageType);
            end

            if isempty(segments)
                error("NCU:EmptyImage", ...
                    "Application image '%s' did not contain any programmable data.", applicationPath);
            end

            totalBytes = 0;
            for i = 1:numel(segments)
                totalBytes = totalBytes + numel(segments(i).Data);
            end
        end

        function segments = i_parse_intel_hex(~, applicationPath)
            lines = splitlines(string(fileread(applicationPath)));
            upperAddress = uint32(0);
            segmentAddress = uint32(0);
            segments = struct('Address', {}, 'Data', {});

            for i = 1:numel(lines)
                line = strtrim(lines(i));
                if strlength(line) == 0
                    continue;
                end

                if extractBefore(line, 2) ~= ":"
                    error("NCU:InvalidHexFile", ...
                        "Invalid Intel HEX record on line %u of '%s'.", i, applicationPath);
                end

                record = extractAfter(line, 1);
                if mod(strlength(record), 2) ~= 0
                    error("NCU:InvalidHexFile", ...
                        "Intel HEX record on line %u of '%s' has an odd number of hex characters.", i, applicationPath);
                end

                byteCount = hex2dec(extractBetween(record, 1, 2));
                offset = uint32(hex2dec(extractBetween(record, 3, 6)));
                recordType = hex2dec(extractBetween(record, 7, 8));

                dataStart = 9;
                dataEnd = dataStart + double(byteCount) * 2 - 1;
                dataHex = "";
                if byteCount > 0
                    dataHex = extractBetween(record, dataStart, dataEnd);
                end

                switch recordType
                    case 0
                        data = zeros(1, double(byteCount), 'uint8');
                        for j = 1:double(byteCount)
                            idx = (j - 1) * 2 + 1;
                            data(j) = uint8(hex2dec(extractBetween(dataHex, idx, idx + 1)));
                        end

                        fullAddress = upperAddress + segmentAddress + offset;
                        segments = i_append_hex_segment(segments, fullAddress, data);

                    case 1
                        break;

                    case 2
                        segmentAddress = bitshift(uint32(hex2dec(dataHex)), 4);
                        upperAddress = uint32(0);

                    case 4
                        upperAddress = bitshift(uint32(hex2dec(dataHex)), 16);
                        segmentAddress = uint32(0);

                    otherwise
                        % Ignore unsupported but non-program-data record types.
                end
            end
        end

        function client = i_open_xcp_client(~, host, port)
            client = struct();
            client.Tcp = tcpclient(host, port, "Timeout", 1);
            client.Counter = uint16(0);
        end

        function i_close_xcp_client(~, client)
            try
                clear client %#ok<NASGU>
            catch
            end
        end

        function [client, connectInfo] = i_xcp_connect(this, client)
            [client, response] = this.i_xcp_command(client, hex2dec('FF'), uint8(0));

            connectInfo = struct();
            connectInfo.Resource.CAL = false;
            connectInfo.Resource.DAQ = false;
            connectInfo.Resource.STIM = false;
            connectInfo.Resource.PGM = false;
            connectInfo.Protection.CAL = false;
            connectInfo.Protection.DAQ = false;
            connectInfo.Protection.STIM = false;
            connectInfo.Protection.PGM = false;
            connectInfo.ByteOrder = "little";
            connectInfo.MaxCTO = uint8(8);
            connectInfo.MaxDTO = uint16(8);

            if numel(response) >= 2
                resource = response(2);
                connectInfo.Resource.CAL = bitget(resource, 1) ~= 0;
                connectInfo.Resource.DAQ = bitget(resource, 3) ~= 0;
                connectInfo.Resource.STIM = bitget(resource, 4) ~= 0;
                connectInfo.Resource.PGM = bitget(resource, 5) ~= 0;
            end

            if numel(response) >= 3
                commModeBasic = response(3);
                if bitget(commModeBasic, 1) ~= 0
                    connectInfo.ByteOrder = "big";
                end
            end

            if numel(response) >= 4
                connectInfo.MaxCTO = response(4);
            end

            if numel(response) >= 6
                connectInfo.MaxDTO = this.i_unpack_u16(response(5:6), connectInfo.ByteOrder);
            end

            try
                [client, statusResponse] = this.i_xcp_command(client, hex2dec('FD'), uint8([]));
                if numel(statusResponse) >= 3
                    protection = statusResponse(3);
                    connectInfo.Protection.CAL = bitget(protection, 1) ~= 0;
                    connectInfo.Protection.DAQ = bitget(protection, 3) ~= 0;
                    connectInfo.Protection.STIM = bitget(protection, 4) ~= 0;
                    connectInfo.Protection.PGM = bitget(protection, 5) ~= 0;
                end
            catch
            end
        end

        function [client, pgmInfo] = i_xcp_get_pgm_processor_info(this, client, connectInfo)
            [client, response] = this.i_xcp_command(client, hex2dec('CE'), uint8([]));

            pgmInfo = struct();
            pgmInfo.AbsoluteMode = false;
            pgmInfo.FunctionalMode = false;
            pgmInfo.NonSequentialProgrammingSupported = false;
            pgmInfo.MaxSector = uint8(0);
            pgmInfo.ByteOrder = connectInfo.ByteOrder;

            if numel(response) >= 2
                props = response(2);
                pgmInfo.AbsoluteMode = bitget(props, 1) ~= 0;
                pgmInfo.FunctionalMode = bitget(props, 2) ~= 0;
                pgmInfo.NonSequentialProgrammingSupported = bitget(props, 7) ~= 0;
            end

            if numel(response) >= 3
                pgmInfo.MaxSector = response(3);
            end
        end

        function [client, pgmSession] = i_xcp_program_start(this, client, connectInfo)
            [client, response] = this.i_xcp_command(client, hex2dec('D2'), uint8([]));

            pgmSession = struct();
            pgmSession.MaxCTO = connectInfo.MaxCTO;
            pgmSession.MaxBlockSize = uint8(1);
            pgmSession.MinSeparationTime = uint8(0);
            pgmSession.ByteOrder = connectInfo.ByteOrder;

            if numel(response) >= 3
                pgmSession.MaxCTO = response(3);
            end

            if numel(response) >= 4
                pgmSession.MaxBlockSize = response(4);
            end

            if numel(response) >= 5
                pgmSession.MinSeparationTime = response(5);
            end
        end

        function client = i_program_segments(this, client, connectInfo, pgmSession, segments)
            maxProgramData = double(pgmSession.MaxCTO) - 2;
            if maxProgramData <= 0
                error("NCU:XCPInvalidMaxCTO", ...
                    "PROGRAM_START returned an invalid MAX_CTO_PGM value of %u.", pgmSession.MaxCTO);
            end

            for i = 1:numel(segments)
                address = uint32(segments(i).Address);
                data = uint8(segments(i).Data);

                this.i_append_output(sprintf( ...
                    'Programming segment %u/%u at 0x%08X (%u byte(s)).', ...
                    i, numel(segments), address, numel(data)));

                client = this.i_xcp_set_mta(client, connectInfo.ByteOrder, uint8(0), address);
                client = this.i_xcp_program_clear(client, connectInfo.ByteOrder, numel(data));

                client = this.i_xcp_set_mta(client, connectInfo.ByteOrder, uint8(0), address);
                offset = 1;
                while offset <= numel(data)
                    count = min(maxProgramData, numel(data) - offset + 1);
                    chunk = data(offset:offset + count - 1);
                    client = this.i_xcp_program_data(client, chunk);
                    offset = offset + count;

                    if pgmSession.MinSeparationTime > 0 && pgmSession.MinSeparationTime < 255
                        pause(double(pgmSession.MinSeparationTime) / 1000);
                    end
                end

                % End-of-segment marker for PROGRAM mode.
                client = this.i_xcp_program_data(client, uint8([]));
            end
        end

        function client = i_xcp_set_mta(this, client, byteOrder, addressExtension, address)
            payload = uint8([0 0 addressExtension this.i_pack_u32(address, byteOrder)]);
            [client, ~] = this.i_xcp_command(client, hex2dec('F6'), payload);
        end

        function client = i_xcp_program_clear(this, client, byteOrder, clearRange)
            payload = uint8([0 0 0 this.i_pack_u32(uint32(clearRange), byteOrder)]);
            [client, ~] = this.i_xcp_command(client, hex2dec('D1'), payload);
        end

        function client = i_xcp_program_data(this, client, chunk)
            payload = uint8([uint8(numel(chunk)) chunk]);
            [client, ~] = this.i_xcp_command(client, hex2dec('D0'), payload);
        end

        function client = i_xcp_program_reset(this, client)
            [client, ~] = this.i_xcp_command(client, hex2dec('CF'), uint8([]), true);
        end

        function client = i_xcp_disconnect(this, client)
            [client, ~] = this.i_xcp_command(client, hex2dec('FE'), uint8([]), true);
        end

        function [client, response] = i_xcp_command(this, client, pid, payload, allowNoResponse)
            if nargin < 5
                allowNoResponse = false;
            end

            payload = uint8(payload);
            frame = uint8([uint8(pid) payload]);
            frameLength = numel(frame);
            counter = client.Counter;
            client.Counter = uint16(mod(double(client.Counter) + 1, 65536));

            header = uint8([ ...
                bitand(frameLength, 255), ...
                bitshift(frameLength, -8), ...
                bitand(counter, 255), ...
                bitshift(counter, -8)]);

            write(client.Tcp, [header frame]);
            response = this.i_read_xcp_response(client, allowNoResponse);

            if isempty(response)
                return;
            end

            if response(1) == hex2dec('FE')
                error("NCU:XCPNegativeResponse", ...
                    "XCP command 0x%02X failed with error 0x%02X (%s).", ...
                    pid, response(2), i_map_xcp_error(uint8(response(2))));
            end

            if response(1) ~= hex2dec('FF')
                error("NCU:XCPUnexpectedResponse", ...
                    "XCP command 0x%02X returned unexpected response PID 0x%02X.", ...
                    pid, response(1));
            end
        end

        function response = i_read_xcp_response(~, client, allowNoResponse)
            response = uint8([]);
            timeoutSec = 1;
            tStart = tic;

            while toc(tStart) <= timeoutSec
                if client.Tcp.NumBytesAvailable >= 4
                    header = read(client.Tcp, 4, "uint8");
                    frameLength = double(header(1)) + bitshift(double(header(2)), 8);

                    while client.Tcp.NumBytesAvailable < frameLength && toc(tStart) <= timeoutSec
                        pause(0.01);
                    end

                    if client.Tcp.NumBytesAvailable < frameLength
                        break;
                    end

                    response = read(client.Tcp, frameLength, "uint8");
                    return;
                end

                pause(0.01);
            end

            if allowNoResponse
                return;
            end

            error("NCU:XCPTimeout", ...
                "Timed out waiting for an XCP response.");
        end

        function bytes = i_pack_u32(~, value, byteOrder)
            value = uint32(value);
            if byteOrder == "big"
                bytes = uint8([ ...
                    bitshift(value, -24), ...
                    bitand(bitshift(value, -16), 255), ...
                    bitand(bitshift(value, -8), 255), ...
                    bitand(value, 255)]);
            else
                bytes = uint8([ ...
                    bitand(value, 255), ...
                    bitand(bitshift(value, -8), 255), ...
                    bitand(bitshift(value, -16), 255), ...
                    bitshift(value, -24)]);
            end
        end

        function value = i_unpack_u16(~, bytes, byteOrder)
            bytes = uint8(bytes);
            if byteOrder == "big"
                value = uint16(bitshift(uint16(bytes(1)), 8) + uint16(bytes(2)));
            else
                value = uint16(uint16(bytes(1)) + bitshift(uint16(bytes(2)), 8));
            end
        end

        function i_append_output(this, output)
            if nargin < 2 || isempty(output)
                return;
            end

            this.StandardOutput = this.i_append_message(this.StandardOutput, string(output));
        end

        function [isRunning, usedVnt, message] = i_probe_xcp_server(this, host, port)
            isRunning = false;
            usedVnt = false;
            message = "";

            if this.i_can_use_vnt_xcp()
                usedVnt = true;
                [isRunning, message] = this.i_probe_with_vnt(host, port);
                if isRunning
                    return;
                end
            end

            [isRunning, rawMessage] = this.i_probe_with_raw_tcp(host, port);
            if isRunning || strlength(message) == 0
                message = rawMessage;
            end
        end

        function tf = i_can_use_vnt_xcp(~)
            tf = (exist("xcpChannel", "file") == 2) && ...
                 (exist("xcpA2L", "file") == 2) && ...
                 (strlength(i_get_default_a2l_path()) > 0);
        end

        function [isRunning, message] = i_probe_with_vnt(~, host, port)
            isRunning = false;
            message = "";

            try
                a2lObj = xcpA2L(char(i_get_default_a2l_path()));
                xcpCh = xcpChannel(a2lObj, "TCP", char(host), port);
                cleanupObj = onCleanup(@() i_disconnect_xcp_channel(xcpCh)); %#ok<NASGU>
                connect(xcpCh);
                isRunning = true;
                message = "Vehicle Network Toolbox XCP probe succeeded.";
            catch ME
                message = "Vehicle Network Toolbox XCP probe failed: " + string(ME.message);
            end
        end

        function [isRunning, message] = i_probe_with_raw_tcp(this, host, port)
            isRunning = false;
            message = "";

            try
                client = this.i_open_xcp_client(host, port);
                cleanupObj = onCleanup(@() this.i_close_xcp_client(client)); %#ok<NASGU>
                [~, connectInfo] = this.i_xcp_connect(client);
                isRunning = true;
                message = sprintf( ...
                    'Raw TCP XCP probe succeeded. CAL=%d DAQ=%d STIM=%d PGM=%d.', ...
                    connectInfo.Resource.CAL, ...
                    connectInfo.Resource.DAQ, ...
                    connectInfo.Resource.STIM, ...
                    connectInfo.Resource.PGM);
            catch ME
                message = "Raw TCP XCP probe failed: " + string(ME.message);
            end
        end

        function text = i_append_message(~, text, message)
            if strlength(text) == 0
                text = string(message);
            else
                text = text + newline + string(message);
            end
        end
    end
end

function segments = i_append_hex_segment(segments, address, data)
    if isempty(segments)
        segments(1).Address = uint32(address);
        segments(1).Data = uint8(data);
        return;
    end

    lastIndex = numel(segments);
    lastEndAddress = uint32(segments(lastIndex).Address) + uint32(numel(segments(lastIndex).Data));
    if lastEndAddress == uint32(address)
        segments(lastIndex).Data = [segments(lastIndex).Data uint8(data)];
    else
        segments(end + 1).Address = uint32(address); %#ok<AGROW>
        segments(end).Data = uint8(data);
    end
end

function name = i_map_xcp_error(errorCode)
    switch uint8(errorCode)
        case hex2dec('10')
            name = "ERR_CMD_BUSY";
        case hex2dec('11')
            name = "ERR_DAQ_ACTIVE";
        case hex2dec('12')
            name = "ERR_PGM_ACTIVE";
        case hex2dec('20')
            name = "ERR_CMD_UNKNOWN";
        case hex2dec('21')
            name = "ERR_CMD_SYNTAX";
        case hex2dec('22')
            name = "ERR_OUT_OF_RANGE";
        case hex2dec('23')
            name = "ERR_WRITE_PROTECTED";
        case hex2dec('24')
            name = "ERR_ACCESS_DENIED";
        case hex2dec('25')
            name = "ERR_ACCESS_LOCKED";
        case hex2dec('26')
            name = "ERR_PAGE_NOT_VALID";
        case hex2dec('27')
            name = "ERR_MODE_NOT_VALID";
        case hex2dec('28')
            name = "ERR_SEGMENT_NOT_VALID";
        case hex2dec('29')
            name = "ERR_SEQUENCE";
        case hex2dec('2A')
            name = "ERR_DAQ_CONFIG";
        case hex2dec('30')
            name = "ERR_MEMORY_OVERFLOW";
        case hex2dec('31')
            name = "ERR_GENERIC";
        case hex2dec('32')
            name = "ERR_VERIFY";
        case hex2dec('FE')
            name = "ERR_CMD_SYNCH";
        otherwise
            name = "UNKNOWN_XCP_ERROR";
    end
end

function a2lPath = i_get_default_a2l_path()
    thisFile = string(mfilename("fullpath"));
    repoRoot = fileparts(fileparts(fileparts(fileparts(thisFile))));
    candidate = fullfile(repoRoot, "test.a2l");
    if isfile(candidate)
        a2lPath = string(candidate);
    else
        a2lPath = "";
    end
end

function i_disconnect_xcp_channel(xcpCh)
    try
        disconnect(xcpCh);
    catch
    end
end
