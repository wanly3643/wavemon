/*
 * wavemon - a wireless network monitoring aplication
 *
 * Copyright (c) 2001-2002 Jan Morgenstern <jan@jm-music.de>
 *
 * wavemon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * wavemon is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with wavemon; see the file COPYING.  If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "wavemon.h"
#include <locale.h>
#include <setjmp.h>

/* GLOBALS */

/**
 * screen switching table
 * @key_name:	name under which the screen appears in the menu bar
 * @init:	screen initialisation function pointer
 * @loop:	screen update function pointer (connected to menu)
 * @fini:	screen cleanup function pointer
 */
static const struct {
	const char *const	key_name;
	void		 	(*init)(void);
	int			(*loop)(WINDOW *);
	void			(*fini)(void);
} screens[] = {
	[SCR_INFO]	= {
		.key_name = "info",
		.init	  = scr_info_init,
		.loop	  = scr_info_loop,
		.fini	  = scr_info_fini
	},
	[SCR_LHIST]	= {
		.key_name = "lhist",
		.init	  = scr_lhist_init,
		.loop	  = scr_lhist_loop,
		.fini	  = scr_lhist_fini
	},
	[SCR_SCAN]	= {
		.key_name = "scan",
		.init	  = scr_aplst_init,
		.loop	  = scr_aplst_loop,
		.fini	  = scr_aplst_fini
	},
	[SCR_EMPTY_F4]	= {
		.key_name = "",
	},
	[SCR_EMPTY_F5]	= {
		.key_name = "",
	},
	[SCR_EMPTY_F6]	= {
		.key_name = "",
	},
	[SCR_PREFS]	= {
		.key_name = "prefs",
		.init	  = scr_conf_init,
		.loop	  = scr_conf_loop,
		.fini	  = scr_conf_fini
	},
	[SCR_HELP]	= {
		.key_name = "help",
		.init	  = scr_help_init,
		.loop	  = scr_help_loop,
		.fini	  = scr_help_fini
	},
	[SCR_ABOUT]	= {
		.key_name = "about",
		.init	  = scr_about_init,
		.loop	  = scr_about_loop,
		.fini	  = scr_about_fini
	},
	[SCR_QUIT]	= {
		.key_name = "quit",
	}
};

/*
 * SIGWINCH handling with buffer synchronisation variable
 */
static volatile sig_atomic_t	env_winch_ready;

const char *str_true = "true";
const char *str_false = "false";

const char* bool_str(bool bln) {
	if (bln) {
		return str_true;
	} else {
		return str_false;
	}
}

void print_as_json(wifi_stat *p_stat) {
	const char *template = 
		"{\n"
		"\t\"interface\": \"%s\",\n"
		"\t\"link_quality\": %d,\n"
		"\t\"signal_level\": %d,\n"
		"\t\"noise_level\": %d,\n"
		"\t\"ssnr\": %d,\n"
		"\t\"rx\": %d,\n"
		"\t\"rx_drop\": %ld,\n"
		"\t\"tx\": %d,\n"
		"\t\"tx_retries\": %d,\n"
		"\t\"tx_failed\": %d,\n"
		"\t\"connected_time\": %d,\n"
		"\t\"inactive_time\": %d,\n"
		"\t\"mode\": \"%s\",\n"
		"\t\"channel\": %d,\n"
		"\t\"frequency\": %d,\n"
		"\t\"long_preamble\": %s,\n"
		"\t\"short_slot\": %s,\n"
		"\t\"cts_protection\": %s,\n"
		"\t\"wme\": %s,\n"
		"\t\"tdls\": %s,\n"
		"\t\"mfp\": %s,\n"
		"\t\"ssid\": \"%s\"\n"
		"}\n";

	printf(template, 
		p_stat->interface,
		p_stat->level.link_quality,
		p_stat->level.signal_level,
		p_stat->level.noise_level,
		p_stat->level.ssnr,
		p_stat->statistics.rx_packets,
		p_stat->statistics.rx_drop_misc,
		p_stat->statistics.tx_packets,
		p_stat->statistics.tx_retries,
		p_stat->statistics.tx_failed,
		p_stat->statistics.connected_time,
		p_stat->statistics.inactive_time,
		p_stat->mode,
		p_stat->channel,
		p_stat->freq,
		bool_str(p_stat->long_preamble),
		bool_str(p_stat->short_slot_time),
		bool_str(p_stat->flag_cts_protection),
		bool_str(p_stat->flag_wme),
		bool_str(p_stat->flag_tdls),
		bool_str(p_stat->flag_mfp),
		p_stat->ssid
	);
}

int main(int argc, char *argv[])
{
	getconf(argc, argv);

	// Initiate
	conf_get_interface_list();

	// Retrieve information
	wifi_stat stat;
	get_wifi_stat(&stat);

	print_as_json(&stat);

	return EXIT_SUCCESS;
}
