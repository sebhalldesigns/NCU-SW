a2l_obj = xcpA2L('test.a2l');
xcp_ch = xcpChannel(a2l_obj, 'UDP', '192.168.1.50', 5005);

if ~isempty(a2l_obj.Warnings)
    disp('A2L parser warnings:');
    disp(a2l_obj.Warnings);
end

disp('A2L events parsed by MATLAB:');
disp(a2l_obj.Events);

connect(xcp_ch);

eng = readMeasurement(xcp_ch, "bButtonState")

writeCharacteristic(xcp_ch, "bLedState", uint8(1))

try
    if isempty(a2l_obj.Events)
        error("No events parsed from A2L.");
    end
    events = string(a2l_obj.Events);
    if any(events == "100 ms")
        eventName = "100 ms";
    else
        eventName = events(1);
    end
    fprintf("Using event: %s\n", eventName);
    createMeasurementList(xcp_ch, "DAQ", eventName, ["bButtonState"]);
catch ME
    fprintf("createMeasurementList failed: %s\n", ME.message);
    fprintf("DAQ setup skipped.\n");
    disconnect(xcp_ch);
    return;
end

startMeasurement(xcp_ch);
pause(0.5);

try
    daqData = readDAQ(xcp_ch, "bButtonState");
    fprintf("readDAQ path used, class=%s\n", class(daqData));
    disp(daqData);
catch ME
    fprintf("readDAQ failed: %s\n", ME.message);
    daqData = readDAQListData(xcp_ch);
    fprintf("readDAQListData path used, class=%s\n", class(daqData));
    disp(daqData);

    if isa(daqData, 'uint8') && numel(daqData) == 8
        asDouble = typecast(uint8(daqData), 'double');
        fprintf("readDAQListData interpreted as IEEE754 double: %g\n", asDouble);
    end
end

stopMeasurement(xcp_ch);
freeMeasurementLists(xcp_ch);

disconnect(xcp_ch);
