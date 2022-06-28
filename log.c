#include "log.h"

const char* log_file = "event_log.csv";

// https://stackoverflow.com/a/3536261
void init_vector(event_vector *ev, size_t size)
{
    ev->events = malloc(size * sizeof(delayed_event));
    ev->used = 0;
    ev->size = size;
}

void append_to_vector(event_vector *ev, delayed_event event)
{
    // upgrade allocated memory if necessary
    if(ev->used >= ev->size)
    {
        ev->size *= 2;
        ev->events = realloc(ev->events, ev->size * sizeof(delayed_event));
    }
    ev->events[ev->used++] = event;
}

void free_vector(event_vector *ev)
{
    free(ev->events);
    ev->events = NULL;
    ev->used = ev->size = 0;
}

void write_event_log(event_vector *ev)
{
    // write header if file doesn't exist
    if(access(log_file, F_OK) != 0)
    {
        FILE *file = fopen(log_file, "w+");
        const char* header = "timestamp;delay;type;value;code\n";
        fwrite(header, 1, strlen(header), file);
        fclose(file);
    }

    FILE *file = fopen(log_file, "a");
    for(int i=0; i<ev->used; ++i)
    {
        delayed_event evnt = ev->events[i];
        fprintf(file,
                "%lu;%i;%i;%i;%i\n",
                evnt.timestamp,
                evnt.delay,
                evnt.type,
                evnt.value,
                evnt.code);
    }
    fclose(file);
    free_vector(ev);
}