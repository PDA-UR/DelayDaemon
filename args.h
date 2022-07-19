#ifndef _ARGS_H_
#define _ARGS_H_

#include <stdlib.h>
#include <argp.h>
#include <string.h>

struct arguments
{
    char* device_file;
    int min_key_delay;
    int max_key_delay;
    int min_move_delay;
    int max_move_delay;
    char* distribution;
    float mean;
    float std;
    char* fifo_path;
    int verbose;
};

error_t parse_args(int argc, char **argv, struct arguments *arg);

#endif