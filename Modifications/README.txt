Copy this file to the corresponding folder to overwrite the existing file there, or make changes according
to the description below.

* Updater.h: (under esp8266/x.x.x/cores/esp8266 folder which can be found in your local app folder in your profile under windows, %LOCALAPPDATA% can be entered into the run window to open this folder)

Add a public 'reset()' function that calls the private _reset() function, in order to allow OTA update to abort upon failure in device key authorization and reset upon error.

