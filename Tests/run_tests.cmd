@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /I..\ReceptorESP32\src /I..\Common\GnssMonitor /I..\Common\RadioControl /I..\Common\Rtcm3 /I..\Common\RtcmRadio /I..\Common\UartProtocol src\main.cpp src\control_tests.cpp src\gnss_tests.cpp src\ntrip_tests.cpp ..\Common\GnssMonitor\GnssMonitor.cpp ..\Common\Rtcm3\Crc24Q.cpp ..\Common\Rtcm3\Rtcm3Parser.cpp ..\Common\RtcmRadio\RtcmRadioProtocol.cpp ..\Common\UartProtocol\UartFrame.cpp /Fe:protocol_tests.exe
if errorlevel 1 exit /b 1
protocol_tests.exe
