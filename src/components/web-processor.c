/*
*  Copyright (C) 2016 TangoCash
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
#include <getopt.h>

#include "web-processor.h"

#include "../color.h"
#include "../util.h"
#include "../log.h"

#define LOGNAME               "web-light: "

struct ambitv_web_processor_priv
{
	int webcolor;
	struct
	{
		int r, g, b;
	} color;
};

static int
ambitv_web_processor_handle_frame(
	struct ambitv_processor_component *component,
	void *nothing,
	int change,
	int r,
	int g,
	int b
)
{
	struct ambitv_web_processor_priv *priv = (struct ambitv_web_processor_priv *) component->priv;

	if (change)
	{
		priv->color.r = r;
		priv->color.g = g;
		priv->color.b = b;
		return 0;
	}
	else
		return (priv->color.r << 16) + (priv->color.g << 8) + (priv->color.b);
}

static int
ambitv_web_processor_update_sink(
	struct ambitv_processor_component *processor,
	struct ambitv_sink_component *sink)
{
	int i, n_out, ret = 0;
	struct ambitv_web_processor_priv *priv = (struct ambitv_web_processor_priv *) processor->priv;

	if (sink->f_num_outputs && sink->f_map_output_to_point && sink->f_set_output_to_rgb)
	{
		n_out = sink->f_num_outputs(sink);

		for (i = 0; i < n_out; i++)
		{
			sink->f_set_output_to_rgb(sink, i, priv->color.r, priv->color.g, priv->color.b);
		}
	}
	else
		ret = -1;

	if (sink->f_commit_outputs)
		sink->f_commit_outputs(sink);

	return ret;
}

static int
ambitv_web_processor_configure(struct ambitv_web_processor_priv *priv, int argc, char **argv)
{
	int c, ret = 0;

	static struct option lopts[] =
	{
		{ "color", required_argument, 0, 'c' },
		{ NULL, 0, 0, 0 }
	};

	optind = 0;
	while (1)
	{
		c = getopt_long(argc, argv, "c:", lopts, NULL);

		if (c < 0)
			break;

		switch (c)
		{
			case 'c':
			{
				if (NULL != optarg)
				{
					if ((sscanf(optarg, "%2X%2X%2X", &priv->color.r, &priv->color.g, &priv->color.b) == 3))
					{
						priv->webcolor = !(priv->color.r || priv->color.g || priv->color.b);
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
			default:
				break;
		}
	}

	if (optind < argc)
	{
		ambitv_log(ambitv_log_error, LOGNAME "extraneous argument '%s'.\n", argv[optind]);
		ret = -1;
	}

errReturn:
	return ret;
}

static void
ambitv_web_processor_print_configuration(struct ambitv_processor_component *component)
{
	struct ambitv_web_processor_priv *priv = (struct ambitv_web_processor_priv *) component->priv;

	ambitv_log(ambitv_log_info,
		"\tcolor : %02X%02X%02X\n", priv->color.r, priv->color.g, priv->color.b
	);
}

static void
ambitv_web_processor_free(struct ambitv_processor_component *component)
{
	free(component->priv);
}

struct ambitv_processor_component *
ambitv_web_processor_create(const char *name, int argc, char **argv)
{
	struct ambitv_processor_component *web_processor =
		ambitv_processor_component_create(name);

	if (NULL != web_processor)
	{
		struct ambitv_web_processor_priv *priv =
			(struct ambitv_web_processor_priv *)malloc(sizeof(struct ambitv_web_processor_priv));

		web_processor->priv = (void *)priv;

		web_processor->f_print_configuration  = ambitv_web_processor_print_configuration;
		web_processor->f_consume_frame        = ambitv_web_processor_handle_frame;
		web_processor->f_update_sink          = ambitv_web_processor_update_sink;
		web_processor->f_free_priv            = ambitv_web_processor_free;

		if (ambitv_web_processor_configure(priv, argc, argv) < 0)
			goto errReturn;
	}

	return web_processor;

errReturn:
	ambitv_processor_component_free(web_processor);
	return NULL;
}
