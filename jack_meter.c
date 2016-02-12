/*

	jackmeter.c
	Simple console based Digital Peak Meter for JACK
	Copyright (C) 2005  Nicholas J. Humfrey

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 */

//for sigalrm catching
#include <signal.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <jack/jack.h>
#include <getopt.h>
#include "config.h"

extern int errno;
#define WAITING_SLEEP_TIME		999999	//999 ms steps
#define MAX_CHANNELS			16

unsigned int sleepTimeUs = 0;
unsigned int slept = 0;
volatile unsigned char time_to_print=0;
int verbosity = 1;

//printed info rows before drawing first scale
unsigned int printed_info_rows = 0;

//default 1 channel
unsigned int channels = 1;
float bias = 1.0f;
float peak[MAX_CHANNELS];// = 0.0f;

int dpeak[MAX_CHANNELS];
int dtime[MAX_CHANNELS];
long int decay_len;
int rate = 8;
int userRate = 8;
char *server_name = NULL;
jack_port_t *input_port[MAX_CHANNELS];
jack_client_t *client = NULL;
jack_options_t options = JackNoStartServer;


/* Read and reset the recent peak sample */
static float read_peak(unsigned int inPortNumber)
{
	if (inPortNumber>=MAX_CHANNELS) return 0;
	float tmp = peak[inPortNumber];
	peak[inPortNumber] = 0.0f;
	return tmp;
}


/* Callback called by JACK when audio is available.
   Stores value of peak sample
   Common callback for all the ports
 */
static int process_peak(jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in;
	unsigned int i;

	/* get the audio samples, and find the peak sample */
	unsigned int inPortNumber = 0;

	// check all ports, but skip those with NULL pointer
	for (;inPortNumber<channels;inPortNumber++){

		/* just incase the port isn't registered yet,
		 * skip it, but try the next in the row
		 */
		if (input_port[inPortNumber] == NULL) continue;

		in = (jack_default_audio_sample_t *) jack_port_get_buffer(input_port[inPortNumber], nframes);
		for (i = 0; i < nframes; i++) {
			const float s = fabs(in[i]);
			if (s > peak[inPortNumber]) {
				peak[inPortNumber] = s;
			}
		}
	}

	return 0;
}


/*
	db: the signal stength in db
	width: the size of the meter
 */
static int iec_scale(float db, int size) {
	float def = 0.0f; /* Meter deflection %age */

	if (db < -70.0f) {
		def = 0.0f;
	} else if (db < -60.0f) {
		def = (db + 70.0f) * 0.25f;
	} else if (db < -50.0f) {
		def = (db + 60.0f) * 0.5f + 2.5f;
	} else if (db < -40.0f) {
		def = (db + 50.0f) * 0.75f + 7.5;
	} else if (db < -30.0f) {
		def = (db + 40.0f) * 1.5f + 15.0f;
	} else if (db < -20.0f) {
		def = (db + 30.0f) * 2.0f + 30.0f;
	} else if (db < 0.0f) {
		def = (db + 20.0f) * 2.5f + 50.0f;
	} else {
		def = 100.0f;
	}

	return (int)( (def / 100.0f) * ((float) size) );
}


/* Close down JACK when exiting */
static void cleanup()
{
	const char **all_ports;
	unsigned int i;

	if (verbosity>0){
		fprintf(stderr,"cleanup()\n");
		printed_info_rows++;
	}
	int inPortNumber=0;
	for (;inPortNumber<channels;inPortNumber++){
		if (input_port[inPortNumber] != NULL ) {

			all_ports = jack_port_get_all_connections(client, input_port[inPortNumber]);

			for (i=0; all_ports && all_ports[i]; i++) {
				jack_disconnect(client, all_ports[i], jack_port_name(input_port[inPortNumber]));
			}
		}
	}

	/* Leave the jack graph */
	jack_client_close(client);

}

/* Connect the chosen port to ours */
static void connect_port(jack_client_t *client, char *port_name, unsigned int inPortNumber)
{
	// overflowing in ports to the last meter port
	if (inPortNumber>=channels) inPortNumber=channels-1;
	jack_port_t *port;

	// Get the port we are connecting to
	port = jack_port_by_name(client, port_name);
	if (port == NULL) {
		fprintf(stderr, "Can't find port '%s'\n", port_name);
		exit(1);
	}

	// Connect the port to our input port
	if (verbosity>0){
		fprintf(stderr,"Connecting '%s' to '%s'...\n", jack_port_name(port), jack_port_name(input_port[inPortNumber]));
		printed_info_rows++;
	}
	if (jack_connect(client, jack_port_name(port), jack_port_name(input_port[inPortNumber]))) {
		if (verbosity>0){
			fprintf(stderr, "Cannot connect port '%s' to '%s'\n", jack_port_name(port), jack_port_name(input_port[inPortNumber]));
		}
		exit(1);
	}
}

/* Display how to use this program */
static int usage( const char * progname )
{
	fprintf(stderr, "jackmeter version %s\n\n", VERSION);
	fprintf(stderr, "Usage %s [-f freqency] [-r ref-level] [-w width] [-s servername] [-n] [-c channels] [-v verbositylevel] [<port>, ...]\n\n", progname);
	fprintf(stderr, "where  -f      is how often to update the meter per second [8]\n");
	fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter\n");
	fprintf(stderr, "       -w      is how wide to make the meter [79]\n");
	fprintf(stderr, "       -s      is the [optional] name given the jack server when it was started\n");
	fprintf(stderr, "       -n      changes mode to output meter level as number in decibels\n");
	fprintf(stderr, "       -p      amount of ports [1], max %u\n", MAX_CHANNELS);
	fprintf(stderr, "       -v      sets level of verbosity [1], max 10\n");
	fprintf(stderr, "       <port>  the port(s) to monitor (rest ports are mixed to last meter port)\n");
	exit(1);
}


void display_connected_ports( unsigned int inPortNumber )
{
	const char **all_ports;
	unsigned int j;

	if (input_port[inPortNumber] != NULL ) {

		all_ports = jack_port_get_all_connections(client, input_port[inPortNumber]);
		printf("\33[2K");
		for (j=0; all_ports && all_ports[j]; j++) {
			fprintf(stderr, "%s, ",all_ports[j]);
		}

		if (j==0){
			fprintf(stderr, "not connected");
		}

		fprintf(stderr, "\n");
		printed_info_rows++;

	} else {
		if (verbosity>0){
			fprintf(stderr, "error\n");
			printed_info_rows++;
		}
	}
}

void display_scale( int width )
{
	int i=0;
	const int marks[11] = { 0, -5, -10, -15, -20, -25, -30, -35, -40, -50, -60 };
	char *scale = malloc( width+1 );
	char *line = malloc( width+1 );

	// Initialise the scale
	for(i=0; i<width; i++) { scale[i] = ' '; line[i]='_'; }
	scale[width] = 0;
	line[width] = 0;

	// 'draw' on each of the db marks
	for(i=0; i < 11; i++) {
		char mark[5];
		int pos = iec_scale( marks[i], width )-1;
		int spos, slen;

		// Create string of the db value
		snprintf(mark, 4, "%d", marks[i]);

		// Position the label string
		slen = strlen(mark);
		spos = pos-(slen/2);
		if (spos<0) spos=0;
		if (spos+strlen(mark)>width) spos=width-slen;
		memcpy( scale+spos, mark, slen );

		// Position little marker
		line[pos] = '|';
	}

	// Print it to screen
	printf("%s\n", scale);
	printf("%s\n", line);
	free(scale);
	free(line);
}


void display_meter( int db, int width, unsigned int inPortNumber )
{
	if (inPortNumber>=MAX_CHANNELS) return;
	int size = iec_scale( db, width );
	int i;

	if (size > dpeak[inPortNumber]) {
		dpeak[inPortNumber] = size;
		dtime[inPortNumber] = 0;
	} else if (dtime[inPortNumber]++ > decay_len) {
		dpeak[inPortNumber] = size;
	}

	printf("\r");

	for(i=0; i<size-1; i++) { printf("#"); }

	if (dpeak[inPortNumber]==size) {
		printf("I");
	} else {
		printf("#");
		for(i=0; i<dpeak[inPortNumber]-size-1; i++) { printf(" "); }
		printf("I");
	}

	for(i=0; i<width-dpeak[inPortNumber]; i++) { printf(" "); }
}

/* Timer signal catcher.
 * Will be called after period of time again and again
 */
void time_handler( int sig ){
	//if not fast enough printing, make slower
	if (time_to_print==1){
		slept-=sleepTimeUs;//WAITING_SLEEP_TIME;
		rate--;
		if (rate<=0) rate = 1;
		decay_len = (long int)(1.6f / (1.0f/rate));
		sleepTimeUs = 1000000.0f/rate;
		//renew ualarm with new sleepTime
		ualarm( sleepTimeUs, sleepTimeUs );
	} else {
		if (rate<userRate) rate++;
	}
	//flag for printing
	time_to_print = 1;
}


int main(int argc, char *argv[])
{
	int console_width = 79;
	jack_status_t status;
	int running = 1;
	float ref_lev;
	int decibels_mode = 0;
	int opt;

	//clear screen
	printf(")\033[2J");
	//move cursor
	printf("\033[%u;%uf", 0,0);

	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	while ((opt = getopt(argc, argv, "p:s:w:f:r:v:nh")) != -1) {
		switch (opt) {
		case 's':
			server_name = (char *) malloc (sizeof (char) * strlen(optarg));
			strcpy (server_name, optarg);
			options |= JackServerName;
			break;
		case 'r':
			ref_lev = atof(optarg);
			if (verbosity>0){
				fprintf(stderr,"Reference level: %.1fdB\n", ref_lev);
				printed_info_rows++;
			}
			bias = powf(10.0f, ref_lev * -0.05f);
			break;
		case 'f':
			userRate = atoi(optarg);
			rate = userRate;
			if (verbosity>0){
				fprintf(stderr,"Updates per second: %d\n", rate);
				printed_info_rows++;
			}
			break;
		case 'w':
			console_width = atoi(optarg);
			if (verbosity>0){
				fprintf(stderr,"Console Width: %d\n", console_width);
				printed_info_rows++;
			}
			break;
		case 'n':
			decibels_mode = 1;
			break;
		case 'p':
			channels = atoi(optarg);
			if (channels>MAX_CHANNELS) channels = MAX_CHANNELS;
			break;
		case 'v':
			verbosity = atoi(optarg);
			break;
		case 'h':
		default:
			/* Show usage/version information */
			usage( argv[0] );
			break;
		}
	}

	// set callback function for timer
	signal( SIGALRM, time_handler );
	sleepTimeUs = 1000000.0f/rate;

	//usleep works with max 999999 us
	if (sleepTimeUs>=1000000) sleepTimeUs = 999999;

	ualarm( sleepTimeUs, sleepTimeUs );

	// Register client with Jack
	if ((client = jack_client_open("meter", options, &status, server_name)) == 0) {
		if (verbosity>0){
			fprintf(stderr, "Failed to start jack client: %d\n", status);
		}
		exit(1);
	}
	if (verbosity>0){
		fprintf(stderr,"Registering as '%s'.\n", jack_get_client_name( client ) );
		printed_info_rows++;
	}

	//register port(s) with jack
	unsigned int i = 0;
	char portname[7];
	for (;i<channels;i++){
		sprintf(portname, "in_%u", i+1);
		// Create our input port
		if (!(input_port[i] = jack_port_register(client, portname, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
			if (verbosity>0){
				fprintf(stderr, "Cannot register input port '%s'.\n", portname);
			}
			exit(1);
		}
	}

	// Register the cleanup function to be called when program exits
	atexit( cleanup );

	// Register the peak signal callback, common for all ports on this client
	jack_set_process_callback(client, process_peak, 0);

	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate client.\n");
		exit(1);
	}

	//on which port to connect, overflow will be handled inside connect_port function
	unsigned int portTo=0;
	// Connect our port(s) to specified port(s)
	if (argc > optind) {
		while (argc > optind) {
			connect_port( client, argv[ optind ], portTo );
			optind++;
			portTo++;
		}
	} else {
		if (verbosity>0){
			fprintf(stderr,"Meter is not connected to a port.\n");
			printed_info_rows++;
		}
	}
	// Calculate the decay length (should be 1600ms)
	decay_len = (long int)(1.6f / (1.0f/rate));

	//where to print first meter with scales. Row from up to down.
	unsigned int startRow = 1+printed_info_rows;
	//connections rows
	unsigned int connectionRows = 1;
	//rows reserved for meter bar
	unsigned int meterRows = 1;
	//rows reserved for scale bar
	unsigned int scaleRows = 2;
	//rows reserved for scale bar

	//just sum
	unsigned int totalRows = scaleRows+meterRows+connectionRows;

	while (running) {
		slept = 0;
		unsigned int inPortNumber=0;
		for (;inPortNumber<channels;inPortNumber++){
			//move cursor
			printf("\033[%u;%uf", startRow+inPortNumber*totalRows,0);
			//display content, scale and connected ports
			display_connected_ports( inPortNumber );
			display_scale( console_width );
			float db = 20.0f * log10f(read_peak(inPortNumber) * bias);

			//display content, meter
			if (decibels_mode==1) {
				printf("%1.1f\n", db);
			} else {
				display_meter( db, console_width, inPortNumber );
			}
		}
		/*
		 * waiting here while it is time to print again
		 */
		while (time_to_print==0){
			if (usleep(WAITING_SLEEP_TIME)==-1){
				//catch reason to be awaken
				int errsv = errno;
				if (errsv==EINTR){
					//awaken by interval timer
					break;
				} else if (errsv==EINVAL) {
					//unsupported sleep time
					exit(1);
				}
			}
		}
		time_to_print=0;

		fprintf(stderr, "\n");
		printed_info_rows++;

		if (verbosity>1){
			fprintf(stderr, "SleepTime %lu ms, rate = %d   \n",sleepTimeUs/1000, rate);
		}
	}
	return 0;
}

