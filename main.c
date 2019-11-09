// created by Andreas Schmid, 2019
// this software is released into the public domain, do whatever you want with it
//
// Usage: latency_daemon [event_handle] [min_delay_move] [max_delay_move] [fifo_path]
// event_handle: path to input device you want to delay (e.g. /dev/input/event5)
// min_delay_click: minimum delay to be added to click events (in milliseconds)
// max_delay_click: maximum delay to be added to click events (in milliseconds)
// min_delay_move: minimum delay to be added to mouse movement (in milliseconds)
// max_delay_move: maximum delay to be added to mouse movement (in milliseconds)
// fifo_path: path to a FIFO used to remotely set delay times during runtime (optional)
// Use the same value for min and max to achieve constant delays.

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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

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

int input_fd = -1;   // actual input device
int virtual_fd = -1; // virtual device for delayed events
int fifo_fd = -1;    // path to FIFO for remotely controlled delay times

char* event_handle; // event handle of the input event we want to add delay to (normally somewhere in /dev/input/)

char* fifo_path;
pthread_t fifo_thread; 

// use attributes to create threads in a detached state
pthread_attr_t invoked_event_thread_attr;

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

// wait for some time, then emit an input event to a virtual input device
void *invoke_delayed_event(void *args) 
{ 
    delayed_event *event = args;

    int eventFd = event->fd;
    int eventType = event->type;
    int eventCode = event->code;
    int eventValue = event->value;
    int eventDelay = event->delay;

    free(event);

    usleep(eventDelay * 1000); // wait for the specified delay time (in milliseconds)

    emit(eventFd, eventType, eventCode, eventValue); // this is the actual delayed input event (eg. mouse move or click)
    emit(eventFd, EV_SYN, SYN_REPORT, 0); // EV_SYN events have to come in time so we trigger them manually

    pthread_exit(NULL);
} 

// thread to handle external modification of delay times using a FIFO
void *handle_fifo(void *args)
{
    char buffer[80];

    // needed so we don't lose our old delay times in case something goes wrong
    int buffer_min_delay_click, buffer_max_delay_click, buffer_min_delay_move, buffer_max_delay_move;

    while(1)
    {
        // open the FIFO - this call blocks the thread until someone writes to the FIFO
        fifo_fd = open(fifo_path, O_RDONLY);

        if(read(fifo_fd, buffer, 80) <= 0) continue; // read the FIFO's content into a buffer and skip setting the variables if an error occurs

        // parse new values from the FIFO
        // only set the delay times if all four values could be read correctly
        if(sscanf(buffer, "%d %d %d %d", &buffer_min_delay_click, &buffer_max_delay_click, &buffer_min_delay_move, &buffer_max_delay_move) == 4)
        {
            // set delay times
            min_delay_click = buffer_min_delay_click;
            max_delay_click = buffer_max_delay_click;
            min_delay_move = buffer_min_delay_move;
            max_delay_move = buffer_max_delay_move;

            // make sure max >= min
            if(max_delay_click < min_delay_click) max_delay_click = min_delay_click;
            if(max_delay_move < min_delay_move) max_delay_move = min_delay_move;

            if(DEBUG) printf("set new values: %d %d %d %d\n", min_delay_click, max_delay_click, min_delay_move, max_delay_move);
        }
        else
        {
            if(DEBUG) printf("could not set new delays - bad data structure\n");
        }

        close(fifo_fd);
    }
}

// create a FIFO for inter process communication at the path defined by the 6th command line parameter (recommended: somewhere in /tmp)
// this can be used to adjust the delay values with an external program during runtime
// simply write (or echo) four numbers (min_delay_click max_delay_click min_delay_move max_delay move) separated by whitespaces into the FIFO
int init_fifo()
{
    unlink(fifo_path); // unlink the FIFO if it already exists
    umask(0); // needed for permissions, I have no idea what this exactly does
    if(mkfifo(fifo_path, 0666) == -1) return 0; // create the FIFO

    // create a thread reading the FIFO and adjusting the delay times
    pthread_create(&fifo_thread, NULL, handle_fifo, NULL); 

    return 1;
}

// create a virtual input device
// this device is used to trigger delayed input events
// source: https://www.kernel.org/doc/html/v4.12/input/uinput.html
int init_virtual_input()
{
    struct uinput_setup usetup;

    virtual_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

    if(!virtual_fd)
    {
        printf("Error - Could not open virtual device\n");
        return 0;
    }

    // enable mouse buttons and relative events
    // for devices other than mice, change this block
    // possible events of input devices can be found using the program evtest
    // the meaning of those key codes can be found here: https://www.kernel.org/doc/html/v4.15/input/event-codes.html
    ioctl(virtual_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(virtual_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(virtual_fd, UI_SET_KEYBIT, BTN_RIGHT);

    ioctl(virtual_fd, UI_SET_EVBIT, EV_REL);
    ioctl(virtual_fd, UI_SET_RELBIT, REL_X);
    ioctl(virtual_fd, UI_SET_RELBIT, REL_Y);
    ioctl(virtual_fd, UI_SET_RELBIT, REL_WHEEL);

    // some metadata for the input device...
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234; // sample vendor
    usetup.id.product = 0x5678; // sample product
    strcpy(usetup.name, "DelayDaemon");

    // actually create the device...
    ioctl(virtual_fd, UI_DEV_SETUP, &usetup);
    ioctl(virtual_fd, UI_DEV_CREATE);

    return 1;
}

// open the input device we want to "enhance" with delay
int init_input_device()
{
    if(DEBUG) printf("input event: %s\n", event_handle);

    input_fd = open(event_handle, O_RDONLY | O_NONBLOCK);

    if(DEBUG) printf("input device fd: %d\n", input_fd);

    if(!input_fd)
    {
        printf("Error - Device not found: %d\n", input_fd);
        return 0;
    }

    // this line reserves the device for this program so its events do not arrive at other applications
    ioctl(input_fd, EVIOCGRAB, 1);

    return 1;
}

// make sure to clean up when the program ends
void onExit(int signum)
{
    // end inter process communication
    pthread_cancel(fifo_thread);
    unlink(fifo_path);

    // close virtual input device
    ioctl(virtual_fd, UI_DEV_DESTROY);
    close(virtual_fd);

    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) 
{
    signal(SIGINT, onExit);

    // check arguments
    // event_handle is mandatory
    // if only one delay value is passed, the added delay is constant
    if(argc <= 2)
    {
        printf("Too few arguments!\n"
               "Usage: latency_daemon [event_handle] [min_delay_move] [max_delay_move] [fifo_path]\n"
               "event_handle: path to input device you want to delay (e.g. /dev/input/event5)\n"
               "min_delay_click: minimum delay to be added to click events (in milliseconds)\n"
               "max_delay_click: maximum delay to be added to click events (in milliseconds)\n"
               "min_delay_move: minimum delay to be added to mouse movement (in milliseconds)\n"
               "max_delay_move: maximum delay to be added to mouse movement (in milliseconds)\n"
               "fifo_path: path to a FIFO used to remotely set delay times during runtime (optional)\n"
               "Use the same value for min and max to achieve constant delays.\n");
        return 1;
    }

    event_handle = argv[1];

    if(!init_input_device()) return 1;
    if(!init_virtual_input()) return 1;

    if(sscanf(argv[2], "%d", &min_delay_click) == EOF) min_delay_click = 0;
    if(sscanf(argv[3], "%d", &max_delay_click) == EOF) max_delay_click = min_delay_click;
    if(sscanf(argv[4], "%d", &min_delay_move) == EOF) min_delay_move = 0;
    if(sscanf(argv[5], "%d", &max_delay_move) == EOF) max_delay_move = min_delay_move;

    // path to a FIFO to enable inter process communication for remotely controlling the delay times (optional)
    if(argc > 6)
    {
        fifo_path = argv[6];
        if(!init_fifo()) return 1;
    }

    if(DEBUG) printf("click delay: %d - %d\nmove delay: %d - %d\n", min_delay_click, max_delay_click, min_delay_move, max_delay_move);

    srand(time(0));

    struct input_event inputEvent;
    int err = -1;

    pthread_attr_setdetachstate(&invoked_event_thread_attr, PTHREAD_CREATE_DETACHED);

    // wait for new input events of the actual device
    // when new event arrives, generate a delay value and create a thread waiting for this delay time
    // the thread then generates an input event for a virtual input device
    // note EV_SYN events are NOT delayed, they are automatically generated when the delayed event is executed
    while(1)
    {
        err = read(input_fd, &inputEvent, sizeof(struct input_event));
        if(err > -1 && inputEvent.type != EV_SYN && inputEvent.type != EV_MSC) // I have no idea what EV_MSC is but it freezes the application (MSC_SCAN!) when moving fast
        {
            delayed_event *event = malloc(sizeof(delayed_event));
            event->fd = virtual_fd;
            event->type = inputEvent.type;
            event->code = inputEvent.code;
            event->value = inputEvent.value;

            if(inputEvent.type == EV_KEY) event->delay = calculate_delay(min_delay_click, max_delay_click);
            else if(inputEvent.type == EV_REL) event->delay = calculate_delay(min_delay_move, max_delay_move);

            pthread_t delayed_event_thread; 
	    pthread_create(&delayed_event_thread, &invoked_event_thread_attr, invoke_delayed_event, event);
        }
    }

    return 0;
}
