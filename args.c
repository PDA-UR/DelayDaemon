#include "args.h"

static char doc[] =
	"DelayDaemon 1.1 -- A GNU/Linux tool to add (varying) latency to input devices\n"
	"Run as superuser!\n";

const char *argp_program_version =
	"DelayDaemon 1.1";

static char args_doc[] =
	"--input <FILE> --min_key_delay <NUM> --max_key_delay <NUM>";

static struct argp_option options[] =
{
	{"input", 'i', "FILE", 0, "/dev/input/eventX"},
	{"min_key_delay", '0', "NUM", 0, "Minimum delay for keys/clicks"},
	{"max_key_delay", '1', "NUM", 0, "Maximum delay for keys/clicks"},
	{"min_move_delay", '2', "NUM", 0, "Minimum delay for mouse movement"},
	{"max_move_delay", '3', "NUM", 0, "Maximum delay for mouse movement"},
	{"distribution", 'd', "STRING", OPTION_ARG_OPTIONAL, "[linear] (default) or [normal] distributed random values"},
	{"mean", 'm', "NUM", OPTION_ARG_OPTIONAL, "target mean value for normal distribution"},
	{"std", 's', "NUM", OPTION_ARG_OPTIONAL, "target standard distribution for normal distribution"},
	{"fifo", 'f', "FILE", OPTION_ARG_OPTIONAL, "path to the fifo file"},
	{"verbose", 'v', "NUM", OPTION_ARG_OPTIONAL, "turn on debug prints"},
	{0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *args = state->input;

	switch (key) {
	case 'i':
        args->device_file = arg;
        break;
    case '0':
        args->min_key_delay = strtol(arg, NULL, 10);
        break;
    case '1':
        args->max_key_delay = strtol(arg, NULL, 10);
        break;
    case '2':
        args->min_move_delay = strtol(arg, NULL, 10);
        break;
    case '3':
        args->max_move_delay = strtol(arg, NULL, 10);
        break;
    case 'd':
        args->distribution = arg + 1;   // skip the '=' character
        break;
    case 'm':
        args->mean = strtol(arg, NULL, 10);
        break;
    case 's':
        args->std = strtol(arg, NULL, 10);
        break;
    case 'f':
        args->fifo_path = arg +1;
        break;
    case 'v':
        args->verbose = 1;
        break;
	case ARGP_KEY_END:

		/* Check if file is specified. */
		if (args->device_file == NULL)
        {
			argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
		}

        if(args->min_key_delay > 0 && args->max_key_delay == 0)
        {
            args->max_key_delay = args->min_key_delay;
        }
        // set default values if none specified
        if(strcmp(args->distribution, "normal") == 0)
        {
            if(args->mean == 0) args->mean = (args->max_key_delay + args->min_key_delay) / 2;
            if(args->std == 0) args->std = args->mean / 10;

            if(args->mean > args->max_key_delay
            || args->mean < args->min_key_delay
            ||(args->mean > args->max_move_delay && args->max_move_delay > 0)   // since move delay is optional and can be 0
            || args->mean < args->min_move_delay)
            {
                printf("Illegal value for mu. Average must be between min and max delay!\n");
                return 1;
            }
        }
		break;
	}

	return 0;
}

error_t parse_args(int argc, char **argv, struct arguments *args)
{
	struct argp argp = {options, parse_opt, args_doc, doc};

	return argp_parse(&argp, argc, argv, 0, 0, args);
}