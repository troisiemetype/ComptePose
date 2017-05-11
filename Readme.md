#Compte Pose.

##This Arduino skecth is for running a Photographic timer, than can be used with enlarger or UV print box.

It basicely has three state:
*A setting state, which is used to set an exposure time.
*A run state, during which it triggers the enlarger / print box.
*A pause state, where the count down is stopped, and the enlarger / print box shut.

A rotary encoder is used to set the duration os exposure, and a push button to start, pause, or stop exposure.

The time increment when setting is 1s under 2 minutes, 5 seconds between 2 and 5 minutes, and 15 seconds above.

The push button launch the timer when in setting mode. When running, it can pause/unpause, and stop exposure if long pressed.

The sketch is based on the following classes, available on my github:
[Timer](https://www.github.com/troisiemtype/Timer): it deals with timing.
[SevenSegmentsDisplay](https://www.github.com/troisiemtype/7segmentdisplay): manage 4 digits display
[Encoder](https://www.github.com/troisiemtype/Encoder): managed encoder
[PushButton](https://www.github.com/troisiemtype/PushButton): provides debouncing and convenience function for button read.