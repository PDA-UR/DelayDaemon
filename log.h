#ifndef _LOG_H_
#define _LOG_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

typedef struct
{
    int type;                   // event type (e.g. key press, relative movement, ...)
    int code;                   // event code (e.g. for key pressses the key/button code)
    int value;                  // event value (e.g. 0/1 for button up/down, coordinates for absolute movement, ...)
    int delay;                  // delay time for the event in milliseconds
    unsigned long timestamp;    // time the event occured
} delayed_event;

typedef struct
{
    size_t size;
    size_t used;
    delayed_event* events;
} event_vector;

void init_vector(event_vector *ev, size_t size);
void append_to_vector(event_vector *ev, delayed_event event);
void free_vector(event_vector *ev);
void write_event_log(event_vector *ev);

#endif