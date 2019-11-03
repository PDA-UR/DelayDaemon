# DelayDaemon
Small program that can be used to add delay to input events.

It grabs all input events from the specified input device and blocks them from being passed to other applications.
A new virtual input device is created and grabbed events are passed to this device after a certain delay.

It is possible to add a fixed delay to all events (by using the same value for **min** and **max**) or a range of possible delay times which leads to a distribution.
For now, only an even distribution is implemented.

The delays for click events and movement events can be set separately.
Note that a varying delay for movement events leads to stuttering mouse movement.

## Usage:

```
DelayDaemon [event_handle] [min_delay_click] [max_delay_click] [min_delay_move] [max_delay_move]
```

 * **event_handle**: path to input device, e.g. `/dev/input/event5`
 * **min_delay_click**: minimum delay to be added to mouse clicks (in milliseconds)
 * **max_delay_click**: maximum delay to be added to mouse clicks (in milliseconds)
 * **min_delay_move**: minimum delay to be added to mouse movement (in milliseconds)
 * **max_delay_move**: maximum delay to be added to mouse movement (in milliseconds)

## Future Work:

 * add the possibility to use different distributions (like gaussian)
 * add inter-process communication capabilities so the delay times can be adjusted by other programs at runtime
