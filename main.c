// created by Andreas Schmid, 2019
// edited by Thomas Fischer, 2022
// partly based on evlag by Filip Aláč
//
// Use the same value for min and max to achieve constant delays.

#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <pthread.h> 
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <math.h>
#include "args.h"
#include "log.h"
#include <libevdev/libevdev.h>

struct arguments args;
int DEBUG = 0;

event_vector ev;     // vector of all input events

char* event_handle; // event handle of the input event we want to add delay to (normally somewhere in /dev/input/)

int fifo_fd = -1;    // path to FIFO for remotely controlled delay times
char* fifo_path;
pthread_t fifo_thread; 

// use attributes to create threads in a detached state
pthread_attr_t invoked_event_thread_attr, log_delay_val_thread_attr;

enum{
    linear,
    normal
} distribution;

// normal distribution variables
double mu = -1.0;
double sigma = -1.0;

// delay range for key events
int min_delay_key = -1;
int max_delay_key = -1;

// delay range for mouse movement
// note that variance here causes the movement to stutter
int min_delay_move = -1;
int max_delay_move = -1;

struct libevdev *event_dev = NULL;
struct libevdev *uinput_dev = NULL;
int polling_rate = 8192;

// returns a normally distributed value around an average mu with std sigma
// source: https://phoxis.org/2013/05/04/generating-random-numbers-from-normal-distribution-in-c/
int randn (double mu, double sigma)
{
  double U1, U2, W, mult;
  static double X1, X2;
  static int call = 0;
 
  if (call == 1)
    {
      call = !call;
      return (mu + sigma * (double) X2);
    }
 
  do
    {
      U1 = -1 + ((double) rand () / RAND_MAX) * 2;
      U2 = -1 + ((double) rand () / RAND_MAX) * 2;
      W = pow (U1, 2) + pow (U2, 2);
    }
  while (W >= 1 || W == 0);
 
  mult = sqrt ((-2 * log (W)) / W);
  X1 = U1 * mult;
  X2 = U2 * mult;
 
  call = !call;
 
  return (mu + sigma * (double) X1);
}

// generate a delay time for an input event
// this function uses a linear distribution between min_delay_move and max_delay_move
// other distributions (e.g. gaussian) may be added in the future
int calculate_delay(int min, int max)
{
    if(min == max) return min; // add constant delay if no range is specified
    else if(distribution == linear) return min + (rand() % (max - min));
    else if(distribution == normal)
    {
        int x = -1;
        while(x < min || x > max)
        {
            x = randn(mu, sigma);
        }
        return x;
    }
    else return 0;
}

// wait for some time, then emit an input event to a virtual input device
void *invoke_delayed_event(void *args) 
{ 
    delayed_event *event = args;

    usleep(event->delay * 1000); // wait for the specified delay time (in milliseconds)

    int rc = libevdev_uinput_write_event(
            uinput_dev, event->type,
            event->code, event->value);

    if(rc != 0) printf("Failed to write uinput event: %s\n", strerror(-rc));

    rc = libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);

    pthread_exit(NULL);
}

// thread to handle external modification of delay times using a FIFO
void *handle_fifo(void *args)
{
    char buffer[80];

    // needed so we don't lose our old delay times in case something goes wrong
    int buffer_min_delay_key, buffer_max_delay_key, buffer_min_delay_move, buffer_max_delay_move;

    while(1)
    {
        // open the FIFO - this call blocks the thread until someone writes to the FIFO
        fifo_fd = open(fifo_path, O_RDONLY);

        if(read(fifo_fd, buffer, 80) <= 0) continue; // read the FIFO's content into a buffer and skip setting the variables if an error occurs

        // parse new values from the FIFO
        // only set the delay times if all four values could be read correctly
        if(sscanf(buffer, "%d %d %d %d", &buffer_min_delay_key, &buffer_max_delay_key, &buffer_min_delay_move, &buffer_max_delay_move) == 4)
        {
            // set delay times
            min_delay_key = buffer_min_delay_key;
            max_delay_key = buffer_max_delay_key;
            min_delay_move = buffer_min_delay_move;
            max_delay_move = buffer_max_delay_move;

            // make sure max >= min
            if(max_delay_key < min_delay_key) max_delay_key = min_delay_key;
            if(max_delay_move < min_delay_move) max_delay_move = min_delay_move;

            if(DEBUG) printf("set new values: %d %d %d %d\n", min_delay_key, max_delay_key, min_delay_move, max_delay_move);
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
// simply write (or echo) four numbers (min_delay_key max_delay_key min_delay_move max_delay move) separated by whitespaces into the FIFO
int init_fifo()
{
    unlink(fifo_path); // unlink the FIFO if it already exists
    umask(0); // needed for permissions, I have no idea what this exactly does
    if(mkfifo(fifo_path, 0666) == -1) return 0; // create the FIFO

    // create a thread reading the FIFO and adjusting the delay times
    pthread_create(&fifo_thread, NULL, handle_fifo, NULL); 

    return 1;
}

// open the input device we want to "enhance" with delay
int init_input_device()
{
	/* Open device. */
	int fd_event = open(event_handle, O_RDONLY);
	if (fd_event < 0)
    {
		perror("Failed to open input device");
		exit(EXIT_FAILURE);
	}

	/* Create libevdev device and grab it. */
	if (libevdev_new_from_fd(fd_event, &event_dev) < 0)
    {
		perror("Failed to init libevdev");
		exit(EXIT_FAILURE);
	}

	if (libevdev_grab(event_dev, LIBEVDEV_GRAB) < 0)
    {
		perror("Failed to grab device");
		exit(EXIT_FAILURE);
	}

    return 1;
}

// create a virtual input device
// this device is used to trigger delayed input events
// source: https://www.freedesktop.org/software/libevdev/doc/latest/group__uinput.html#gaf14b21301bac9d79c20e890172873b96
int init_virtual_input()
{
    /* Create uinput clone of device. */
	int fd_uinput = open("/dev/uinput", O_WRONLY);
	if (fd_uinput < 0)
    {
		perror("Failed to open uinput device");
		exit(EXIT_FAILURE);
	}

	if (libevdev_uinput_create_from_device(event_dev, fd_uinput, &uinput_dev) < 0)
    {
		perror("Failed to create uinput device");
		exit(EXIT_FAILURE);
	}

    return 1;
}

// get the next input event from libevdev
int get_event(struct input_event *event)
{
    struct timeval current_time;
	gettimeofday(&current_time, NULL);

	int rc = LIBEVDEV_READ_STATUS_SUCCESS;

    rc = libevdev_next_event(event_dev,
                    LIBEVDEV_READ_FLAG_NORMAL |
                    LIBEVDEV_READ_FLAG_BLOCKING, event);

    /* Handle dropped SYN. */
    if (rc == LIBEVDEV_READ_STATUS_SYNC)
    {
        printf("Warning, syn dropped: (%d) %s\n", -rc, strerror(-rc));

        while (rc == LIBEVDEV_READ_STATUS_SYNC)
        {
            rc = libevdev_next_event(event_dev,
                    LIBEVDEV_READ_FLAG_SYNC, event);
        }
    }

	if (rc == -ENODEV)
    {
		printf("Device disconnected: (%d) %s\n", -rc, strerror(-rc));
        return -1;
	}
    return 1;
}

// make sure to clean up when the program ends
void onExit(int signum)
{
    printf("\n");
    write_event_log(&ev);

    // end inter process communication
    pthread_cancel(fifo_thread);
    unlink(fifo_path);

    exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) 
{
    signal(SIGINT, onExit);
    // defaults
	args.device_file = NULL;
    args.min_key_delay = 0;
    args.max_key_delay = 0;
    args.min_move_delay = 0;
    args.max_move_delay = 0;
    args.distribution = "";

	if (parse_args(argc, argv, &args) < 0) {
		perror("Failed to parse arguments");
		exit(EXIT_FAILURE);
	}

    // set global variables
    event_handle = args.device_file;
    min_delay_key = args.min_key_delay;
    max_delay_key = args.max_key_delay;
    min_delay_move = args.min_move_delay;
    max_delay_move = args.max_move_delay;
    mu = args.mean;
    sigma = args.std;
    if(strcmp(args.distribution, "normal")) distribution = normal;
    else distribution = linear;
    DEBUG = args.verbose;

    // prevents Keydown events for KEY_Enter from never being released when grabbing the input device
    // after running the program in a terminal by pressing Enter
    // https://stackoverflow.com/questions/41995349
    sleep(1);

    init_vector(&ev, 10);
    if(!init_input_device()) return 1;
    if(!init_virtual_input()) return 1;

    if(distribution==normal && DEBUG) printf("Normal distribution: mean: %lf, std: %lf\n", mu, sigma);

    if(DEBUG) printf("key delay: %d - %d\nmove delay: %d - %d\n", min_delay_key, max_delay_key, min_delay_move, max_delay_move);

    srand(time(0));

    pthread_attr_setdetachstate(&invoked_event_thread_attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setdetachstate(&log_delay_val_thread_attr, PTHREAD_CREATE_DETACHED);

    // wait for new input events of the actual device
    // when new event arrives, generate a delay value and create a thread waiting for this delay time
    // the thread then generates an input event for a virtual input device
    // note EV_SYN events are NOT delayed, they are automatically generated when the delayed event is executed
    struct input_event inputEvent;
    int err = -1;

    while(1)
    {
        err = get_event(&inputEvent);
        if(err > -1 && inputEvent.type != EV_SYN)
		{
            delayed_event *event = malloc(sizeof(delayed_event));
            event->type = inputEvent.type;
            event->code = inputEvent.code;
            event->value = inputEvent.value;

            if(inputEvent.type == EV_KEY) event->delay = calculate_delay(min_delay_key, max_delay_key);
            else if(inputEvent.type == EV_REL) event->delay = calculate_delay(min_delay_move, max_delay_move);

            pthread_t delayed_event_thread; 
            pthread_create(&delayed_event_thread, &invoked_event_thread_attr, invoke_delayed_event, event);

            event->timestamp = inputEvent.time.tv_sec * 1000 + inputEvent.time.tv_usec / 1000;
            append_to_vector(&ev, *event);
        }
    }
    
    return 0;
}
