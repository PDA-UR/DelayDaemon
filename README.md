# DelayDaemon
Small program that can be used to add delay to input events.

It grabs all input events from the specified input device and blocks them from being passed to other applications.
A new virtual input device is created and grabbed events are passed to this device after a certain delay.

It is possible to add a fixed delay to all events (by using the same value for **min** and **max**) or a range of possible delay times which leads to a distribution.
Delays within range can distributed linearly or normally.

The delays for click events and movement events can be set separately.
Note that a varying delay for movement events leads to stuttering mouse movement.

The delay times can also be changed during runtime using a FIFO.

This program must be run as superuser, unless your user has permissions to access uinput.

## Usage:
```
DelayDaemon [OPTION...]
            --input <FILE> --min_key_delay <NUM> --max_key_delay <NUM>
```
```
-0, --min_key_delay=NUM    Minimum delay for keys/clicks
-1, --max_key_delay=NUM    Maximum delay for keys/clicks
-2, --min_move_delay=NUM   Minimum delay for mouse movement
-3, --max_move_delay=NUM   Maximum delay for mouse movement
-d, --distribution[=STRING]   [linear] (default) or [normal] distributed
                             random values
-f, --fifo[=FILE]          path to the fifo file
-i, --input=FILE           /dev/input/eventX
-m, --mean[=NUM]           target mean value for normal distribution
-s, --std[=NUM]            target standard distribution for normal
                             distribution
-v, --verbose              turn on debug prints
-?, --help                 Give this help list
    --usage                Give a short usage message
-V, --version              Print program version
```
Example:
```
sudo ./DelayDaemon -0 0 -1 100 -2 0 -3 200 -i /dev/input/event6 
```

This will set each click delay to a random value between 0 and 100 and each mouse movement to a random value between 0 and 200 for the input device corresponding to event6.

## Remotely Controlling Delay Times

If `--fifo` is set, a FIFO is created at this path.
By writing into this FIFO (which can be done with normal file operations), delay times can be changed during runtime.
The new values have to be written to the FIFO seperated by whitespaces and all four values have to be set.

**Example:**

```
sudo ./DelayDaemon -i /dev/input/event6 -0 100 -1 200 -2 0 -3 0 -f /tmp/delaydaemon
echo "200 300 0 0" > /tmp/delaydaemon
```

This would start the program with a click delay of 100-200 ms and then increase the delay to 200-300 ms.
