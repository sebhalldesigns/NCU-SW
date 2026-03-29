function blkStruct = slblocks
    Browser.Library = 'ncukit';      % library model name (without .slx)
    Browser.Name    = 'NCU Kit';         % shown in Library Browser
    Browser.IsFlat  = 0;

    blkStruct.Name    = 'NCU Kit';
    blkStruct.OpenFcn = 'ncukit';
    blkStruct.MaskInitialization = '';
    blkStruct.Browser = Browser;
end