// created by Andreas Schmid, 2019
// this software is released into the public domain, do whatever you want with it

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h> 

// set to 1 for more verbose console output
#define DEBUG 0

typedef struct
{
    int fd;     // file descriptor of the input device
    int type;   // event type (e.g. key press, relative movement, ...)
    int code;   // event code (e.g. for key pressses the key/button code)
    int value;  // event value (e.g. 0/1 for button up/down, coordinates for absolute movement, ...)
    int delay;  // delay time for the event in milliseconds
} delayed_event;

int inputFD = -1;
int virtualFD = -1;

char* eventHandle;

// delay range for mouse clicks
int min_delay_click = -1;
int max_delay_click = -1;

// delay range for mouse movement
// note that variance here causes the movement to stutter
int min_delay_move = -1;
int max_delay_move = -1;

// generate a delay time for an input event
// this function uses a linear distribution between min_delay_move and max_delay_move
// other distributions (e.g. gaussian) may be added in the future
int calculate_delay(int min, int max)
{
    if(min == max) return min; // add constant delay if no range is specified
    else return min + (rand() % (max - min));
}

// creates an input event for the specified device
// source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
void emit(int fd, int type, int code, int val)
{
    struct input_event ie;
    
    ie.type = type;
    ie.code = code;
    ie.value = val;
    
    ie.time.tv_sec = 0;
    ie.time.tv_usec = 0;
    
    write(fd, &ie, sizeof(ie));
}

// create a virtual input device
// this device is used to trigger delayed input events
// source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
int init_virtual_input()
{
    struct uinput_setup usetup;

    virtualFD = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if(!virtualFD)
    {
        printf("Error - Could not open virtual device\n");
        return 0;
    }

    // enable mouse buttons and relative events
    // for devices other than mice, change this block
    // possible events of input devices can be found using the program evtest
    // the meaning of those key codes can be found here: https://www.kernel.org/doc/html/v4.15/input/event-codes.html
    ioctl(virtualFD, UI_SET_EVBIT, EV_KEY);
    ioctl(virtualFD, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(virtualFD, UI_SET_KEYBIT, BTN_RIGHT);

    ioctl(virtualFD, UI_SET_EVBIT, EV_REL);
    ioctl(virtualFD, UI_SET_RELBIT, REL_X);
    ioctl(virtualFD, UI_SET_RELBIT, REL_Y);

    // some metadata for the input device...
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234; // sample vendor
    usetup.id.product = 0x5678; // sample product
    strcpy(usetup.name, "DelayDaemon");

    // actually create the device...
    ioctl(virtualFD, UI_DEV_SETUP, &usetup);
    ioctl(virtualFD, UI_DEV_CREATE);

    return 1;
}

// open the input device we want to "enhance" with delay
int init_input_device()
{
    if(DEBUG) printf("input event: %s\n", eventHandle);

    inputFD = open(eventHandle, O_RDONLY | O_NONBLOCK);

    if(DEBUG) printf("input device fd: %d\n", inputFD);

    if(!inputFD)
    {
        printf("Error - Device not found: %d\n", inputFD);
        return 0;
    }

    // this line reserves the device for this program so its events do not arrive at other applications
    ioctl(inputFD, EVIOCGRAB, 1);

    return 1;
}

// wait for some time, then emit an input event to a virtual input device
void *invoke_delayed_event(void *args) 
{ 
    delayed_event *event = args;

    usleep(event->delay * 1000); // wait for the specified delay time (in milliseconds)

    emit(event->fd, event->type, event->code, event->value); // this is the actual delayed input event (eg. mouse move or click)
    emit(event->fd, EV_SYN, SYN_REPORT, 0); // EV_SYN events have to come in time so we trigger them manually

    free(event);
    return 0; 
} 

int main(int argc, char* argv[]) 
{
    // check arguments
    // eventHandle is mandatory
    // if only one delay value is passed, the added delay is constant
    if(argc <= 2)
    {
        printf("Too few arguments!\n"
               "Usage: latency_daemon [event_handle] [min_delay_move] [max_delay_move]\n"
               "event_handle: path to input device you want to delay (e.g. /dev/input/event5)\n"
               "min_delay_click: minimum delay to be added to click events (in milliseconds)\n"
               "max_delay_click: maximum delay to be added to click events (in milliseconds)\n"
               "min_delay_move: minimum delay to be added to mouse movement (in milliseconds)\n"
               "max_delay_move: maximum delay to be added to mouse movement (in milliseconds)\n"
               "Use the same value for min and max to achieve constant delays.\n");
        return 1;
    }

    eventHandle = argv[1];
    if(sscanf(argv[2], "%d", &min_delay_click) == EOF) min_delay_click = 0;
    if(sscanf(argv[3], "%d", &max_delay_click) == EOF) max_delay_click = min_delay_click;
    if(sscanf(argv[4], "%d", &min_delay_move) == EOF) min_delay_move = 0;
    if(sscanf(argv[5], "%d", &max_delay_move) == EOF) max_delay_move = min_delay_move;

    if(DEBUG) printf("click delay: %d - %d\nmove delay: %d - %d\n", min_delay_click, max_delay_click, min_delay_move, max_delay_move);

    if(!init_input_device()) return 1;
    if(!init_virtual_input()) return 1;

    srand(time(0));

    struct input_event inputEvent;
    int err = -1;

    // wait for new input events of the actual device
    // when new event arrives, generate a delay value and create a thread waiting for this delay time
    // the thread then generates an input event for a virtual input device
    // note EV_SYN events are NOT delayed, they are automatically generated when the delayed event is executed
    while(1)
    {
        err = read(inputFD, &inputEvent, sizeof(struct input_event));
        if(err > -1 && inputEvent.type != EV_SYN)
        {
            delayed_event *event = malloc(sizeof(delayed_event));
            event->fd = virtualFD;
            event->type = inputEvent.type;
            event->code = inputEvent.code;
            event->value = inputEvent.value;

            if(inputEvent.type == EV_KEY) event->delay = calculate_delay(min_delay_click, max_delay_click);
            else if(inputEvent.type == EV_REL) event->delay = calculate_delay(min_delay_move, max_delay_move);

            pthread_t delayed_event_thread; 
            pthread_create(&delayed_event_thread, NULL, invoke_delayed_event, event); 
        }
    }

    ioctl(virtualFD, UI_DEV_DESTROY);
    close(virtualFD);

    return 0;
}
