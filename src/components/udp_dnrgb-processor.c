/* ambi-tv: a flexible ambilight clone for embedded linux
 *  Copyright (C) 2013 Georg Kaindl
 *  Copyright (C) 2022 TangoCash
 *
 *  This file is part of ambi-tv.
 *
 *  ambi-tv is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ambi-tv is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ambi-tv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>

#include "udp_dnrgb-processor.h"

#include "../color.h"
#include "../util.h"
#include "../log.h"

#define DEFAULT_PORT     21324
#define MTU      1474

#define LOGNAME               "udp_dnrgb: "

struct ambitv_udp_dnrgb_processor
{
	int port;
	int sockfd;
	struct sockaddr_in servaddr, cliaddr;
};

static int ambitv_udp_dnrgb_processor_handle_frame(struct ambitv_processor_component *component, void *frame,
	int width, int height, int bytesperline, int fmt)
{
	//struct ambitv_udp_dnrgb_processor *dnrgb = (struct ambitv_udp_dnrgb_processor *) component->priv;

	//dnrgb->offset = fmod(dnrgb->offset + dnrgb->step, 1.0);
	//printf("handle_frame: \n");

	return 0;
}

static int ambitv_udp_dnrgb_processor_update_sink(struct ambitv_processor_component *processor,
	struct ambitv_sink_component *sink)
{
	//printf("update sink1: \n");
	int n_out, ret = 0;
	struct pollfd pollStruct[1];

	struct ambitv_udp_dnrgb_processor *dnrgb = (struct ambitv_udp_dnrgb_processor *) processor->priv;

	if (sink->f_num_outputs && sink->f_set_output_to_rgb && dnrgb->sockfd > 0)
	{
        unsigned int len, n;
        unsigned char buffer[MTU];

		n_out = sink->f_num_outputs(sink);
		//printf("n_out : %d \n", n_out);

		pollStruct[0].fd = dnrgb->sockfd;
        pollStruct[0].events = POLLIN;
        pollStruct[0].revents = 0;

        if( poll(pollStruct, 1, 32) == 1)
        {
        //printf("update sink2: \n");
		len = sizeof(dnrgb->cliaddr);  //len is value/result
		n = recvfrom(dnrgb->sockfd, (unsigned char *)buffer, MTU, 0, (struct sockaddr *) &dnrgb->cliaddr, &len);

		printf("Received : %d bytes\n", n);
		printf("Mode : %x\n", buffer[0]);
		printf("Wait : %x\n", buffer[1]);

		uint16_t led = ((buffer[3] << 0) & 0xFF) + ((buffer[2] << 8) & 0xFF00);
		unsigned int count = (n - 4) / 3;
		printf("Led index start from %d count %d\n", led, count);

        for (uint16_t i = 4; i < n -2; i += 3)
        {
            printf("for %d\n",led);
            if (led >= n_out) break;
            sink->f_set_output_to_rgb(sink, led, buffer[i], buffer[i+1], buffer[i+2]);
            printf("<LED %d 0x%02x : 0x%02x : 0x%02x>\n", led, buffer[i], buffer[i+1], buffer[i+2]);
            led++;
        }

		}
	}
	else
		ret = -1;

	if (sink->f_commit_outputs)
		sink->f_commit_outputs(sink);
    //printf("update sink end: \n");
	return ret;
}

static int ambitv_udp_dnrgb_processor_configure(struct ambitv_processor_component *dnrgb, int argc, char **argv)
{
	int c, ret = 0;

	struct ambitv_udp_dnrgb_processor *dnrgb_priv = (struct ambitv_udp_dnrgb_processor *) dnrgb->priv;
	if (NULL == dnrgb_priv)
		return -1;

	static struct option lopts[] =
	{
		{ "port", required_argument, 0, 'p' },
		{ NULL, 0, 0, 0 }
	};

	while (1)
	{
		c = getopt_long(argc, argv, "", lopts, NULL);

		if (c < 0)
			break;

		switch (c)
		{
			case 'p':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					long nbuf = strtol(optarg, &eptr, 10);

					if ('\0' == *eptr && nbuf >= 0)
					{
						dnrgb_priv->port = nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error, LOGNAME "invalid argument for '%s': '%s'.\n", argv[optind - 2],
							optarg);
						return -1;
					}
				}

				break;
			}

			default:
				break;
		}
	}

	if (optind < argc)
	{
		ambitv_log(ambitv_log_error, LOGNAME "extraneous configuration argument: '%s'.\n", argv[optind]);
		ret = -1;
	}

	return ret;
}

static void ambitv_udp_dnrgb_processor_print_configuration(struct ambitv_processor_component *component)
{
	struct ambitv_udp_dnrgb_processor *dnrgb = (struct ambitv_udp_dnrgb_processor *) component->priv;

	ambitv_log(ambitv_log_info, "\tport:  %d\n", dnrgb->port);
}

static void ambitv_udp_dnrgb_processor_free(struct ambitv_processor_component *component)
{
    struct ambitv_udp_dnrgb_processor *dnrgb = (struct ambitv_udp_dnrgb_processor *) component->priv;
    if (dnrgb->sockfd > 0)
    {
        close(dnrgb->sockfd);
    }

	free(dnrgb);
}

struct ambitv_processor_component *
ambitv_udp_dnrgb_processor_create(const char *name, int argc, char **argv)
{
	struct ambitv_processor_component *udp_dnrgb_processor = ambitv_processor_component_create(name);

	if (NULL != udp_dnrgb_processor)
	{
		struct ambitv_udp_dnrgb_processor *priv = (struct ambitv_udp_dnrgb_processor *) malloc(
				sizeof(struct ambitv_udp_dnrgb_processor));

		udp_dnrgb_processor->priv = (void *) priv;

		priv->port = DEFAULT_PORT;
		priv->sockfd = -1;

		udp_dnrgb_processor->f_print_configuration = ambitv_udp_dnrgb_processor_print_configuration;
		//udp_dnrgb_processor->f_consume_frame = ambitv_udp_dnrgb_processor_handle_frame;
		udp_dnrgb_processor->f_update_sink = ambitv_udp_dnrgb_processor_update_sink;
		udp_dnrgb_processor->f_free_priv = ambitv_udp_dnrgb_processor_free;

		if (ambitv_udp_dnrgb_processor_configure(udp_dnrgb_processor, argc, argv) < 0)
			goto errReturn;

        // Creating socket file descriptor
        if ((priv->sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            perror("socket creation failed");
            goto errReturn;
        }

        memset(&priv->servaddr, 0, sizeof(priv->servaddr));
        memset(&priv->cliaddr, 0, sizeof(priv->cliaddr));
        // Filling server information
        priv->servaddr.sin_family    = AF_INET; // IPv4
        priv->servaddr.sin_addr.s_addr = INADDR_ANY;
        priv->servaddr.sin_port = htons(priv->port);

        // Bind the socket with the server address
        if (bind(priv->sockfd, (const struct sockaddr *)&priv->servaddr, sizeof(priv->servaddr)) < 0)
        {
            perror("bind failed");
            goto errReturn;
        }
	}

	return udp_dnrgb_processor;

errReturn: ambitv_processor_component_free(udp_dnrgb_processor);
	return NULL;
}
