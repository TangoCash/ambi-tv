/* ambi-tv: a flexible ambilight clone for embedded linux
*  Copyright (C) 2013 Georg Kaindl
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

/* udp_dnrgb-sink: udp_dnrgb sink for ambi-tv project
*
*  Author: TangoCash
*  https://github.com/tangocash/ambi-tv
*/

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "udp_dnrgb-sink.h"

#include "../util.h"
#include "../log.h"
#include "../color.h"
#include "../parse-conf.h"

#define DEFAULT_UDP_HOST                "127.0.0.1"
#define DEFAULT_UDP_PORT                21324
#define DEFAULT_BRIGHTNESS		        100
#define DEFAULT_GAMMA                   1.6   // works well for me, but ymmv...
#define DNRGB_MODE                      4     // mode 4, wait for 5 seconds
#define DNRGB_WAIT                      5

#define UDP_OUT_MAXSIZE                 1472
#define UDP_OUT_MAX_LED_IDX             489

#define LOGNAME                         "udp: "

static const char scol[][6] =
{ "red", "green", "blue" };

struct ambitv_udp_dnrgb_priv
{
	char              *udp_host;
	int                udp_port;
	int                sockfd, num_leds, actual_num_leds, out_len;
	int                led_len[4], *led_str[4];   // top, bottom, left, right
	double             led_inset[4];              // top, bottom, left, right
	unsigned char     *out;
	unsigned char    **bbuf;
	int                num_bbuf, bbuf_idx;
	int                brightness, intensity[3], intensity_min[3];
	double             gamma[3];      // RGB gamma, not GRB!
	unsigned char     *gamma_lut[3];  // also RGB
	struct sockaddr_in servaddr;
};

static int *ambitv_udp_dnrgb_ptr_for_output(struct ambitv_udp_dnrgb_priv *udp_dnrgb, int output, int *led_str_idx, int *led_idx)
{
	int idx = 0, *ptr = NULL;

	if (output < udp_dnrgb->num_leds)
	{
		while (output >= udp_dnrgb->led_len[idx])
		{
			output -= udp_dnrgb->led_len[idx];
			idx++;
		}

		if (udp_dnrgb->led_str[idx][output] >= 0)
		{
			ptr = &udp_dnrgb->led_str[idx][output];

			if (led_str_idx)
				*led_str_idx = idx;

			if (led_idx)
				*led_idx = output;
		}
	}

	return ptr;
}

static int ambitv_udp_dnrgb_map_output_to_point(struct ambitv_sink_component *component, int output, int width, int height, int *x, int *y)
{
	int ret = -1, *outp = NULL, str_idx = 0, led_idx = 0;
	struct ambitv_udp_dnrgb_priv *udp_dnrgb = (struct ambitv_udp_dnrgb_priv *) component->priv;
	outp = ambitv_udp_dnrgb_ptr_for_output(udp_dnrgb, output, &str_idx, &led_idx);

	if (NULL != outp)
	{
		ret = 0;
		float llen = udp_dnrgb->led_len[str_idx] - 1;
		float dim = (str_idx < 2) ? width : height;
		float inset = udp_dnrgb->led_inset[str_idx] * dim;
		dim -= 2 * inset;

		switch (str_idx)
		{
			case 0:  // top
				*x = (int) CONSTRAIN(inset + (dim / llen) * led_idx, 0, width);
				*y = 0;
				break;

			case 1:  // bottom
				*x = (int) CONSTRAIN(inset + (dim / llen) * led_idx, 0, width);
				*y = height;
				break;

			case 2:  // left
				*x = 0;
				*y = (int) CONSTRAIN(inset + (dim / llen) * led_idx, 0, height);
				break;

			case 3:  // right
				*x = width;
				*y = (int) CONSTRAIN(inset + (dim / llen) * led_idx, 0, height);
				break;

			default:
				ret = -1;
				break;
		}
	}
	else
	{
		*x = *y = -1;
	}

	return ret;
}

static int ambitv_udp_dnrgb_commit_outputs(struct ambitv_sink_component *component)
{
	int ret = -1;
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =
		(struct ambitv_udp_dnrgb_priv *)component->priv;

	if (udp_dnrgb->sockfd < 0)
	{
		udp_dnrgb->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

		if (udp_dnrgb->sockfd < 0)
		{
			ret = udp_dnrgb->sockfd;
			ambitv_log(ambitv_log_error, LOGNAME "failed to open socket '%s' : %d (%s).\n",
				   udp_dnrgb->udp_host, errno, strerror(errno));
			//goto errReturn;
		}
		else
		{
			udp_dnrgb->servaddr.sin_family = AF_INET;
			udp_dnrgb->servaddr.sin_addr.s_addr = inet_addr(udp_dnrgb->udp_host);
			udp_dnrgb->servaddr.sin_port = htons(udp_dnrgb->udp_port);
		}
	}

	if (udp_dnrgb->sockfd >= 0)
	{
		unsigned char buffer[UDP_OUT_MAXSIZE];
		int remain_leds = udp_dnrgb->num_leds;
		unsigned int start_led = 0;
		unsigned int count_led = remain_leds > UDP_OUT_MAX_LED_IDX ? UDP_OUT_MAX_LED_IDX : remain_leds;

		while (remain_leds > 0)
		{
			memset(buffer, 0x00, UDP_OUT_MAXSIZE);
			buffer[0] = DNRGB_MODE;
			buffer[1] = DNRGB_WAIT;
			buffer[2] = start_led >> 8;
			buffer[3] = start_led;

			memcpy(&buffer[4],&udp_dnrgb->out[start_led], count_led*3);

			ret = sendto(udp_dnrgb->sockfd, buffer, count_led * 3 + 4, 0, (const struct sockaddr *)&udp_dnrgb->servaddr, sizeof(udp_dnrgb->servaddr));

			if (ret != count_led * 3 + 4)
			{
				if (ret <= 0)
					ret = -errno;
				else
					ret = -ret;
			}
			else
				ret = 0;

			start_led = count_led;
			count_led = remain_leds - UDP_OUT_MAX_LED_IDX > UDP_OUT_MAX_LED_IDX ? UDP_OUT_MAX_LED_IDX : remain_leds - UDP_OUT_MAX_LED_IDX;
			remain_leds = remain_leds - UDP_OUT_MAX_LED_IDX;
		}
	}

	if (udp_dnrgb->sockfd >= 0)
	{
		close(udp_dnrgb->sockfd);
		udp_dnrgb->sockfd = -1;
	}

	if (udp_dnrgb->num_bbuf)
		udp_dnrgb->bbuf_idx = (udp_dnrgb->bbuf_idx + 1) % udp_dnrgb->num_bbuf;

	return ret;
}

static void ambitv_udp_dnrgb_clear_leds(struct ambitv_sink_component *component)
{
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =
		(struct ambitv_udp_dnrgb_priv *)component->priv;

	if (NULL != udp_dnrgb->out)
	{
		int i;

		// send 3 times, in case there's noise on the line,
		// so that all LEDs will definitely be off afterwards.
		for (i = 0; i < 3; i++)
		{
			memset(udp_dnrgb->out, 0x00, udp_dnrgb->out_len);
			(void)ambitv_udp_dnrgb_commit_outputs(component);
		}
	}
}

static int ambitv_udp_dnrgb_set_output_to_rgb(struct ambitv_sink_component *component,	int idx, int r,	int g, int b)
{
	int ret = -1, *outp = NULL, i, *rgb[] = {&r, &g, &b};
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =	(struct ambitv_udp_dnrgb_priv *)component->priv;
	unsigned char *bptr;

	if (idx >= ambitv_special_sinkcommand_brightness)
	{
		switch (idx)
		{
			case ambitv_special_sinkcommand_brightness:
				if (g)
					ret = udp_dnrgb->brightness;
				else
				{
					udp_dnrgb->brightness = r;
					ambitv_log(ambitv_log_info, LOGNAME "brightness was set to %d%%", r);
					ret = 0;
				}

				break;

			case ambitv_special_sinkcommand_gamma_red:
			case ambitv_special_sinkcommand_gamma_green:
			case ambitv_special_sinkcommand_gamma_blue:
			{
				unsigned char *tptr;
				idx -= ambitv_special_sinkcommand_gamma_red;

				if (g)
					ret = udp_dnrgb->gamma[idx] * 100;
				else
				{
					udp_dnrgb->gamma[idx] = (double) r / 100.0;
					tptr = udp_dnrgb->gamma_lut[idx];
					udp_dnrgb->gamma_lut[idx] = ambitv_color_gamma_lookup_table_create(udp_dnrgb->gamma[idx]);
					ambitv_color_gamma_lookup_table_free(tptr);
					ambitv_log(ambitv_log_info, LOGNAME "gamma-%s was set to %.2f", scol[idx], udp_dnrgb->gamma[idx]);
					ret = 0;
				}
			}
			break;

			case ambitv_special_sinkcommand_intensity_red:
			case ambitv_special_sinkcommand_intensity_green:
			case ambitv_special_sinkcommand_intensity_blue:
			{
				idx -= ambitv_special_sinkcommand_intensity_red;

				if (g)
					ret = udp_dnrgb->intensity[idx];
				else
				{
					udp_dnrgb->intensity[idx] = r / 100;
					ambitv_log(ambitv_log_info, LOGNAME "intensity-%s was set to %d%%", scol[idx],
						   udp_dnrgb->intensity[idx]);
					ret = 0;
				}
			}
			break;

			case ambitv_special_sinkcommand_min_intensity_red:
			case ambitv_special_sinkcommand_min_intensity_green:
			case ambitv_special_sinkcommand_min_intensity_blue:
			{
				idx -= ambitv_special_sinkcommand_min_intensity_red;

				if (g)
					ret = udp_dnrgb->intensity_min[idx];
				else
				{
					udp_dnrgb->intensity_min[idx] = r / 100;
					ambitv_log(ambitv_log_info, LOGNAME "intensity-min-%s was set to %d%%", scol[idx],
						   udp_dnrgb->intensity_min[idx]);
					ret = 0;
				}
			}
			break;
		}

		return ret;
	}

	outp = ambitv_udp_dnrgb_ptr_for_output(udp_dnrgb, idx, NULL, NULL);

	if (NULL != outp)
	{
		int ii = *outp;

		if (udp_dnrgb->num_bbuf)
		{
			unsigned char *acc = udp_dnrgb->bbuf[udp_dnrgb->bbuf_idx];
			acc[3 * ii]             = r;
			acc[3 * ii + 1]         = g;
			acc[3 * ii + 2]         = b;
			r = g = b = 0;

			for (i = 0; i < udp_dnrgb->num_bbuf; i++)
			{
				r += udp_dnrgb->bbuf[i][3 * ii];
				g += udp_dnrgb->bbuf[i][3 * ii + 1];
				b += udp_dnrgb->bbuf[i][3 * ii + 2];
			}

			r /= udp_dnrgb->num_bbuf;
			g /= udp_dnrgb->num_bbuf;
			b /= udp_dnrgb->num_bbuf;
		}

		for (i = 0; i < 3; i++)
		{
			*rgb[i] =
				(unsigned char)(((((int) * rgb[i] * udp_dnrgb->brightness) / 100) * udp_dnrgb->intensity[i]) / 100);

			if (udp_dnrgb->gamma_lut[i])
				*rgb[i] = ambitv_color_map_with_lut(udp_dnrgb->gamma_lut[i], *rgb[i]);
		}

		bptr = udp_dnrgb->out + (3 * ii);

		if (r < udp_dnrgb->intensity_min[0] * 2.55)
			r = udp_dnrgb->intensity_min[0] * 2.55;

		if (g < udp_dnrgb->intensity_min[1] * 2.55)
			g = udp_dnrgb->intensity_min[1] * 2.55;

		if (b < udp_dnrgb->intensity_min[2] * 2.55)
			b = udp_dnrgb->intensity_min[2] * 2.55;

		*(bptr + 0) = r;
		*(bptr + 1) = g;
		*(bptr + 2) = b;
		bptr += 3;
		ret = 0;
	}

	return ret;
}

static int ambitv_udp_dnrgb_num_outputs(struct ambitv_sink_component *component)
{
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =	(struct ambitv_udp_dnrgb_priv *)component->priv;
	return udp_dnrgb->num_leds;
}

static int ambitv_udp_dnrgb_start(struct ambitv_sink_component *component)
{
	int ret = 0;

	ambitv_udp_dnrgb_clear_leds(component);

	return ret;
}

static int ambitv_udp_dnrgb_stop(struct ambitv_sink_component *component)
{
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =	(struct ambitv_udp_dnrgb_priv *)component->priv;
	ambitv_udp_dnrgb_clear_leds(component);

	if (udp_dnrgb->sockfd >= 0)
	{
		close(udp_dnrgb->sockfd);
		udp_dnrgb->sockfd = -1;
	}

	return 0;
}

static int ambitv_udp_dnrgb_configure(struct ambitv_sink_component *component, int argc, char **argv)
{
	int i, c, ret = 0;
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =	(struct ambitv_udp_dnrgb_priv *)component->priv;
	memset(udp_dnrgb->led_str, 0, sizeof(int *) * 4);
	udp_dnrgb->num_leds = udp_dnrgb->actual_num_leds = 0;

	if (NULL == udp_dnrgb)
		return -1;

	static struct option lopts[] =
	{
		{ "udp-host", required_argument, 0, 'd' },
		{ "udp-port", required_argument, 0, 'r' },
		{ "leds-top", required_argument, 0, '0' },
		{ "leds-bottom", required_argument, 0, '1' },
		{ "leds-left", required_argument, 0, '2' },
		{ "leds-right", required_argument, 0, '3' },
		{ "blended-frames", required_argument, 0, 'b' },
		{ "overall-brightness", required_argument, 0, 'o' },
		{ "gamma-red", required_argument, 0, '4'},
		{ "gamma-green", required_argument, 0, '5'},
		{ "gamma-blue", required_argument, 0, '6'},
		{ "intensity-red", required_argument, 0, '7' },
		{ "intensity-green", required_argument, 0, '8' },
		{ "intensity-blue", required_argument, 0, '9' },
		{ "intensity-min-red", required_argument, 0, 'A' },
		{ "intensity-min-green", required_argument, 0, 'B' },
		{ "intensity-min-blue", required_argument, 0, 'C' },
		{ "led-inset-top", required_argument, 0, 'w'},
		{ "led-inset-bottom", required_argument, 0, 'x'},
		{ "led-inset-left", required_argument, 0, 'y'},
		{ "led-inset-right", required_argument, 0, 'z'},
		{ NULL, 0, 0, 0 }
	};

	while (1)
	{
		c = getopt_long(argc, argv, "", lopts, NULL);

		if (c < 0)
			break;

		switch (c)
		{
			case 'd':
			{
				if (NULL != optarg)
				{
					if (NULL != udp_dnrgb->udp_host)
						free(udp_dnrgb->udp_host);

					udp_dnrgb->udp_host = strdup(optarg);
				}

				break;
			}

			case 'r':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					long nbuf = strtol(optarg, &eptr, 10);

					if ('\0' == *eptr && nbuf >= 0)
					{
						udp_dnrgb->udp_port = (int) nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error,
							   LOGNAME "invalid argument for '%s': '%s'.\n", argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case 'b':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					long nbuf = strtol(optarg, &eptr, 10);

					if ('\0' == *eptr && nbuf >= 0)
					{
						udp_dnrgb->num_bbuf = (int)nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error, LOGNAME "invalid argument for '%s': '%s'.\n",
							   argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case 'o':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					long nbuf = strtol(optarg, &eptr, 10);

					if ('\0' == *eptr && nbuf >= 0)
					{
						udp_dnrgb->brightness = (int) nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error,
							   LOGNAME "invalid argument for '%s': '%s'.\n", argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case '0':
			case '1':
			case '2':
			case '3':
			{
				int idx = c - '0';
				ret = ambitv_parse_led_string(optarg, &udp_dnrgb->led_str[idx], &udp_dnrgb->led_len[idx]);

				if (ret < 0)
				{
					ambitv_log(ambitv_log_error, LOGNAME "invalid led configuration string for '%s': '%s'.\n",
						   argv[optind - 2], optarg);
					goto errReturn;
				}

				udp_dnrgb->num_leds += udp_dnrgb->led_len[idx];

				for (i = 0; i < udp_dnrgb->led_len[idx]; i++)
					if (udp_dnrgb->led_str[idx][i] >= 0)
						udp_dnrgb->actual_num_leds++;

				break;
			}

			case '4':
			case '5':
			case '6':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					double nbuf = strtod(optarg, &eptr);

					if ('\0' == *eptr)
					{
						udp_dnrgb->gamma[c - '4'] = nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error, LOGNAME "invalid argument for '%s': '%s'.\n",
							   argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case '7':
			case '8':
			case '9':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					int nbuf = (int) strtol(optarg, &eptr, 10);

					if ('\0' == *eptr)
					{
						udp_dnrgb->intensity[c - '7'] = nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error,
							   LOGNAME "invalid argument for '%s': '%s'.\n", argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case 'A':
			case 'B':
			case 'C':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					int nbuf = (int) strtol(optarg, &eptr, 10);

					if ('\0' == *eptr)
					{
						udp_dnrgb->intensity_min[c - 'A'] = nbuf;
					}
					else
					{
						ambitv_log(ambitv_log_error,
							   LOGNAME "invalid argument for '%s': '%s'.\n", argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
					}
				}

				break;
			}

			case 'w':
			case 'x':
			case 'y':
			case 'z':
			{
				if (NULL != optarg)
				{
					char *eptr = NULL;
					double nbuf = strtod(optarg, &eptr);

					if ('\0' == *eptr)
					{
						udp_dnrgb->led_inset[c - 'w'] = nbuf / 100.0;
					}
					else
					{
						ambitv_log(ambitv_log_error, LOGNAME "invalid argument for '%s': '%s'.\n",
							   argv[optind - 2], optarg);
						ret = -1;
						goto errReturn;
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
		ambitv_log(ambitv_log_error, LOGNAME "extraneous argument: '%s'.\n",
			   argv[optind]);
		ret = -1;
	}

errReturn:
	return ret;
}

static void ambitv_udp_dnrgb_print_configuration(struct ambitv_sink_component *component)
{
	struct ambitv_udp_dnrgb_priv *udp_dnrgb =	(struct ambitv_udp_dnrgb_priv *)component->priv;
	ambitv_log(ambitv_log_info,
		   "\tudp host:          %s\n"
		   "\tudp port:          %d\n"
		   "\tnumber of leds:    %d\n"
		   "\tblending frames:   %d\n"
		   "\tled insets (tblr): %.1f%%, %.1f%%, %.1f%%, %.1f%%\n"
		   "\tbrightness:          %d%%\n"
		   "\tintensity (rgb):     %d%%, %d%%, %d%%\n"
		   "\tintensity-min (rgb): %d%%, %d%%, %d%%\n"
		   "\tgamma (rgb):       %.2f, %.2f, %.2f\n",
		   udp_dnrgb->udp_host,
		   udp_dnrgb->udp_port,
		   udp_dnrgb->actual_num_leds,
		   udp_dnrgb->num_bbuf,
		   udp_dnrgb->led_inset[0] * 100.0, udp_dnrgb->led_inset[1] * 100.0,
		   udp_dnrgb->led_inset[2] * 100.0, udp_dnrgb->led_inset[3] * 100.0,
		   udp_dnrgb->brightness,
		   udp_dnrgb->intensity[0], udp_dnrgb->intensity[1], udp_dnrgb->intensity[2],
		   udp_dnrgb->intensity_min[0], udp_dnrgb->intensity_min[1], udp_dnrgb->intensity_min[2],
		   udp_dnrgb->gamma[0], udp_dnrgb->gamma[1], udp_dnrgb->gamma[2]
		  );
}

void ambitv_udp_dnrgb_free(struct ambitv_sink_component *component)
{
	int i;
	struct ambitv_udp_dnrgb_priv *udp_dnrgb = (struct ambitv_udp_dnrgb_priv *)component->priv;

	if (NULL != udp_dnrgb)
	{
		if (NULL != udp_dnrgb->udp_host)
			free(udp_dnrgb->udp_host);

		if (NULL != udp_dnrgb->out)
			free(udp_dnrgb->out);

		if (NULL != udp_dnrgb->bbuf)
		{
			for (i = 0; i < udp_dnrgb->num_bbuf; i++)
				free(udp_dnrgb->bbuf[i]);

			free(udp_dnrgb->bbuf);
		}

		for (i = 0; i < 3; i++)
		{
			if (NULL != udp_dnrgb->gamma_lut[i])
				ambitv_color_gamma_lookup_table_free(udp_dnrgb->gamma_lut[i]);
		}

		free(udp_dnrgb);
	}
}

struct ambitv_sink_component *ambitv_udp_dnrgb_create(const char *name, int argc, char **argv)
{
	struct ambitv_sink_component *udp_dnrgb = ambitv_sink_component_create(name);

	if (NULL != udp_dnrgb)
	{
		int i;
		struct ambitv_udp_dnrgb_priv *priv = (struct ambitv_udp_dnrgb_priv *)malloc(sizeof(struct ambitv_udp_dnrgb_priv));
		udp_dnrgb->priv = priv;
		memset(priv, 0, sizeof(struct ambitv_udp_dnrgb_priv));
		priv->sockfd         = -1;
		priv->udp_host       = strdup(DEFAULT_UDP_HOST);
		priv->udp_port       = DEFAULT_UDP_PORT;
		priv->brightness     = DEFAULT_BRIGHTNESS;
		priv->gamma[0]       = DEFAULT_GAMMA;
		priv->gamma[1]       = DEFAULT_GAMMA;
		priv->gamma[2]       = DEFAULT_GAMMA;
		bzero((char *)&priv->servaddr, sizeof(struct sockaddr_in));
		udp_dnrgb->f_print_configuration   = ambitv_udp_dnrgb_print_configuration;
		udp_dnrgb->f_start_sink            = ambitv_udp_dnrgb_start;
		udp_dnrgb->f_stop_sink             = ambitv_udp_dnrgb_stop;
		udp_dnrgb->f_num_outputs           = ambitv_udp_dnrgb_num_outputs;
		udp_dnrgb->f_set_output_to_rgb     = ambitv_udp_dnrgb_set_output_to_rgb;
		udp_dnrgb->f_map_output_to_point   = ambitv_udp_dnrgb_map_output_to_point;
		udp_dnrgb->f_commit_outputs        = ambitv_udp_dnrgb_commit_outputs;
		udp_dnrgb->f_free_priv             = ambitv_udp_dnrgb_free;

		if (ambitv_udp_dnrgb_configure(udp_dnrgb, argc, argv) < 0)
			goto errReturn;

		priv->out_len   = sizeof(unsigned char) * 3 * priv->actual_num_leds;
		priv->out      = (unsigned char *)malloc(priv->out_len);

		if (priv->num_bbuf > 1)
		{
			priv->bbuf = (unsigned char **)malloc(sizeof(unsigned char *) * priv->num_bbuf);

			for (i = 0; i < priv->num_bbuf; i++)
			{
				priv->bbuf[i] = (unsigned char *)malloc(priv->out_len);
				memset(priv->bbuf[i], 0, priv->out_len);
			}
		}
		else
			priv->num_bbuf = 0;

		memset(priv->out, 0x00, priv->out_len);

		for (i = 0; i < 3; i++)
		{
			if (priv->gamma[i] >= 0.0)
			{
				priv->gamma_lut[i] =
					ambitv_color_gamma_lookup_table_create(priv->gamma[i]);
			}
		}
	}

	return udp_dnrgb;
errReturn:
	ambitv_sink_component_free(udp_dnrgb);
	return NULL;
}
