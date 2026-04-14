a2l_obj = xcpA2L('test.a2l');
xcp_ch = xcpChannel(a2l_obj, 'UDP', '192.168.1.50', 5005);

connect(xcp_ch);

eng = readMeasurement(xcp_ch, "bButtonState")

writeCharacteristic(xcp_ch, "bLedState", uint8(1))

disconnect(xcp_ch);