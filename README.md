# DelayDaemon
Small program that can be used to add delay to input events.

It grabs all input events from the specified input device and blocks them from being passed to other applications.
A new virtual input device is created and grabbed events are passed to this device after a certain delay.

It is possible to add a fixed delay to all events (by only specifying the **min_delay** parameter) or a range of possible delay times which leads to a distribution.
For now, only an even distribution is implemented.

## Usage:

```
DelayDaemon [event_handle] [min_delay] [max_delay]
```

**event_handle**: path to input device, e.g. `/dev/input/event5`

**min_delay**: minimum delay to be added (in milliseconds)

**max_delay**: maximum delay to be added (in milliseconds, optional)

If **max_delay** is not defined, a constant delay of **min_delay** is added to all events.

## Future Work:

 * add the possibility to use different distributions (like gaussian)
 * add inter-process communication capabilities so the delay times can be adjusted by other programs at runtime
