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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <jack/jack.h>
#include <getopt.h>
#include "config.h"


float bias = 1.0f;
float peak = 0.0f;

int dpeak = 0;
int dtime = 0;
int decay_len;
char *server_name = NULL;
jack_port_t *input_port = NULL;
jack_client_t *client = NULL;
jack_options_t options = JackNoStartServer;


/* Read and reset the recent peak sample */
static float read_peak()
{
	float tmp = peak;
	peak = 0.0f;

	return tmp;
}


/* Callback called by JACK when audio is available.
   Stores value of peak sample */
static int process_peak(jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in;
	unsigned int i;


	/* just incase the port isn't registered yet */
	if (input_port == NULL) {
		return 0;
	}


	/* get the audio samples, and find the peak sample */
	in = (jack_default_audio_sample_t *) jack_port_get_buffer(input_port, nframes);
	for (i = 0; i < nframes; i++) {
		const float s = fabs(in[i]);
		if (s > peak) {
			peak = s;
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

	fprintf(stderr,"cleanup()\n");

	if (input_port != NULL ) {

		all_ports = jack_port_get_all_connections(client, input_port);

		for (i=0; all_ports && all_ports[i]; i++) {
			jack_disconnect(client, all_ports[i], jack_port_name(input_port));
		}
	}

	/* Leave the jack graph */
	jack_client_close(client);

}


/* Connect the chosen port to ours */
static void connect_port(jack_client_t *client, char *port_name)
{
	jack_port_t *port;

	// Get the port we are connecting to
	port = jack_port_by_name(client, port_name);
	if (port == NULL) {
		fprintf(stderr, "Can't find port '%s'\n", port_name);
		exit(1);
	}

	// Connect the port to our input port
	fprintf(stderr,"Connecting '%s' to '%s'...\n", jack_port_name(port), jack_port_name(input_port));
	if (jack_connect(client, jack_port_name(port), jack_port_name(input_port))) {
		fprintf(stderr, "Cannot connect port '%s' to '%s'\n", jack_port_name(port), jack_port_name(input_port));
		exit(1);
	}
}


/* Sleep for a fraction of a second */
static int fsleep( float secs )
{

//#ifdef HAVE_USLEEP
	return usleep( secs * 1000000 );
//#endif
}


/* Display how to use this program */
static int usage( const char * progname )
{
	fprintf(stderr, "jackmeter version %s\n\n", VERSION);
	fprintf(stderr, "Usage %s [-f freqency] [-r ref-level] [-w width] [-s servername] [-n] [<port>, ...]\n\n", progname);
	fprintf(stderr, "where  -f      is how often to update the meter per second [8]\n");
	fprintf(stderr, "       -r      is the reference signal level for 0dB on the meter\n");
	fprintf(stderr, "       -w      is how wide to make the meter [79]\n");
	fprintf(stderr, "       -s      is the [optional] name given the jack server when it was started\n");
	fprintf(stderr, "       -n      changes mode to output meter level as number in decibels\n");
	fprintf(stderr, "       <port>  the port(s) to monitor (multiple ports are mixed)\n");
	exit(1);
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


void display_meter( int db, int width )
{
	int size = iec_scale( db, width );
	int i;
	
	if (size > dpeak) {
		dpeak = size;
		dtime = 0;
	} else if (dtime++ > decay_len) {
		dpeak = size;
	}
	
	printf("\r");
	
	for(i=0; i<size-1; i++) { printf("#"); }
	
	if (dpeak==size) {
		printf("I");
	} else {
		printf("#");
		for(i=0; i<dpeak-size-1; i++) { printf(" "); }
		printf("I");
	}
	
	for(i=0; i<width-dpeak; i++) { printf(" "); }
}


int main(int argc, char *argv[])
{
	int console_width = 79;
	jack_status_t status;
	int running = 1;
	float ref_lev;
	int decibels_mode = 0;
	int rate = 8;
	int opt;

	// Make STDOUT unbuffered
	setbuf(stdout, NULL);

	while ((opt = getopt(argc, argv, "s:w:f:r:nhv")) != -1) {
		switch (opt) {
			case 's':
				server_name = (char *) malloc (sizeof (char) * strlen(optarg));
            			strcpy (server_name, optarg);
				options |= JackServerName;
				break;
			case 'r':
				ref_lev = atof(optarg);
				fprintf(stderr,"Reference level: %.1fdB\n", ref_lev);
				bias = powf(10.0f, ref_lev * -0.05f);
				break;
			case 'f':
				rate = atoi(optarg);
				fprintf(stderr,"Updates per second: %d\n", rate);
				break;
			case 'w':
				console_width = atoi(optarg);
				fprintf(stderr,"Console Width: %d\n", console_width);
				break;
			case 'n':
				decibels_mode = 1;
				break;
			case 'h':
			case 'v':
			default:
				/* Show usage/version information */
				usage( argv[0] );
				break;
		}
	}



	// Register with Jack
	if ((client = jack_client_open("meter", options, &status, server_name)) == 0) {
		fprintf(stderr, "Failed to start jack client: %d\n", status);
		exit(1);
	}
	fprintf(stderr,"Registering as '%s'.\n", jack_get_client_name( client ) );

	// Create our input port
	if (!(input_port = jack_port_register(client, "in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0))) {
		fprintf(stderr, "Cannot register input port 'meter'.\n");
		exit(1);
	}
	
	// Register the cleanup function to be called when program exits
	atexit( cleanup );

	// Register the peak signal callback
	jack_set_process_callback(client, process_peak, 0);


	if (jack_activate(client)) {
		fprintf(stderr, "Cannot activate client.\n");
		exit(1);
	}


	// Connect our port to specified port(s)
	if (argc > optind) {
		while (argc > optind) {
			connect_port( client, argv[ optind ] );
			optind++;
		}
	} else {
		fprintf(stderr,"Meter is not connected to a port.\n");
	}

	// Calculate the decay length (should be 1600ms)
	decay_len = (int)(1.6f / (1.0f/rate));
	

	// Display the scale
	if (decibels_mode==0) {
		display_scale( console_width );
	}

	while (running) {
		float db = 20.0f * log10f(read_peak() * bias);
		
		if (decibels_mode==1) {
			printf("%1.1f\n", db);
		} else {
			display_meter( db, console_width );
		}
		
		fsleep( 1.0f/rate );
	}

	return 0;
}

