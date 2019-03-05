
set EPICS_DISPLAY_PATH=Z:\epics\synApps_6_0\support\areaDetector-R3-4\ADSeaBreeze\SeaBreezeApp\op\adl;Z:\epics\synApps_6_0\support\areaDetector-R3-4\ADCore-R3-4\ADApp\op\adl
echo %EPICS_DISPLAY_PATH%
start medm -x -macro "P=HIBEF:XFEL:, R=cam1:" ADSeaBreeze.adl
