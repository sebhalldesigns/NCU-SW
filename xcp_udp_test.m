a2l_obj = xcpA2L('test.a2l');
xcp_ch = xcpChannel(a2l_obj, 'UDP', '192.168.1.50', 5005);
connect(xcp_ch);
disconnect(xcp_ch);