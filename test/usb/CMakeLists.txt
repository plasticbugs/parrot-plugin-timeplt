# test/usb: a plugin for test/run.sh and sim/test.sh (main.c says what it
# does), with every USB feature on. Its cvcart_features() is what
# test/host/plugin.cmake reads to build it for this computer; it builds
# for the cart too, from a CMakeLists.txt that adds this folder.
add_executable(usbtest main.c)
parrotcart_screen(usbtest)
cvcart_features(usbtest PADS KEYBOARD MOUSE MIDI_IN MIDI_OUT)
cvcart_plugin(usbtest ID parrotcart.usbtest NAME "USB Test" VERSION 1.0.0 BY "Parrot Cart"
              FILE USBTEST.CVP)
