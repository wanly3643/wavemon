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
#include "iw_if.h"
#include "iw_nl80211.h"

/* GLOBALS */
static WINDOW *w_levels, *w_stats, *w_if, *w_info, *w_net;
static pthread_t sampling_thread;
static time_t last_update;
// Global linkstat data, populated by sampling thread.
static struct {
	bool              run;	 // enable/disable sampling
	pthread_mutex_t   mutex; // producer/consumer lock for @data
	struct iw_nl80211_linkstat data;
} linkstat;

/** Sampling pthread shared by info and histogram screen. */
static void *sampling_loop(void *arg)
{
	sigset_t blockmask;

	/* See comment in scan_scr.c for rationale. */
	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &blockmask, NULL);

	do {
		pthread_mutex_lock(&linkstat.mutex);
		iw_nl80211_get_linkstat(&linkstat.data);
		pthread_mutex_unlock(&linkstat.mutex);

		iw_cache_update(&linkstat.data);
	} while (linkstat.run && usleep(conf.stat_iv * 1000) == 0);
	return NULL;
}

void sampling_init(void)
{
	pthread_mutex_init(&linkstat.mutex, NULL);
	linkstat.run = true;
	pthread_create(&sampling_thread, NULL, sampling_loop, NULL);
}

void sampling_stop(void)
{
	linkstat.run = false;
	pthread_join(sampling_thread, NULL);
	pthread_mutex_destroy(&linkstat.mutex);
}

static void display_levels(void)
{
	static float qual, signal, noise, ssnr;
	/*
	 * FIXME: revise the scale implementation. It does not work
	 *        satisfactorily, maybe it is better to have a simple
	 *        solution using 3 levels of different colour.
	 */
	int8_t nscale[2] = { conf.noise_min, conf.noise_max },
	     lvlscale[2] = { -40, -20};
	char tmp[0x100];
	int line;
	bool noise_data_valid;
	int sig_qual = -1, sig_qual_max, sig_level;

	noise_data_valid = iw_nl80211_have_survey_data(&linkstat.data);
	sig_level = linkstat.data.signal_avg ?: linkstat.data.signal;

	/* See comments in iw_cache_update */
	if (sig_level == 0)
		sig_level = linkstat.data.bss_signal;

	for (line = 1; line <= WH_LEVEL; line++)
		mvwclrtoborder(w_levels, line, 1);

	if (linkstat.data.bss_signal_qual) {
		/* BSS_SIGNAL_UNSPEC is scaled 0..100 */
		sig_qual     = linkstat.data.bss_signal_qual;
		sig_qual_max = 100;
	} else if (sig_level) {
		if (sig_level < -110)
			sig_qual = 0;
		else if (sig_level > -40)
			sig_qual = 70;
		else
			sig_qual = sig_level + 110;
		sig_qual_max = 70;
	}

	line = 1;

	/* Noise data is rare. Use the space for spreading out. */
	if (!noise_data_valid)
		line++;

	if (sig_qual == -1) {
		line++;
	} else {
		qual = ewma(qual, sig_qual, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "link quality: ");
		sprintf(tmp, "%0.f%%  ", (1e2 * qual)/sig_qual_max);
		waddstr_b(w_levels, tmp);
		sprintf(tmp, "(%0.f/%d)  ", qual, sig_qual_max);
		waddstr(w_levels, tmp);

		waddbar(w_levels, line++, qual, 0, sig_qual_max, lvlscale, true);
	}

	/* Spacer */
	line++;
	if (!noise_data_valid)
		line++;

	if (sig_level != 0) {
		signal = ewma(signal, sig_level, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "signal level: ");
		sprintf(tmp, "%.0f dBm (%s)", signal, dbm2units(signal));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line, signal, conf.sig_min, conf.sig_max,
			lvlscale, true);
		if (conf.lthreshold_action)
			waddthreshold(w_levels, line, signal, conf.lthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '>');
		if (conf.hthreshold_action)
			waddthreshold(w_levels, line, signal, conf.hthreshold,
				      conf.sig_min, conf.sig_max, lvlscale, '<');
	}

	line++;

	if (noise_data_valid) {
		noise = ewma(noise, linkstat.data.survey.noise, conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "noise level:  ");
		sprintf(tmp, "%.0f dBm (%s)", noise, dbm2units(noise));
		waddstr_b(w_levels, tmp);

		waddbar(w_levels, line++, noise, conf.noise_min, conf.noise_max,
			nscale, false);
	}

	if (noise_data_valid && sig_level) {
		ssnr = ewma(ssnr, sig_level - linkstat.data.survey.noise,
				  conf.meter_decay / 100.0);

		mvwaddstr(w_levels, line++, 1, "SNR:           ");
		sprintf(tmp, "%.0f dB", ssnr);
		waddstr_b(w_levels, tmp);
	}

	wrefresh(w_levels);
}



void print_wifi_levels(wifi_level_stat *p_stat)
{
	static float qual, signal, noise, ssnr;

	bool noise_data_valid;
	int sig_qual = -1, sig_qual_max, sig_level;

	noise_data_valid = iw_nl80211_have_survey_data(&linkstat.data);
	sig_level = linkstat.data.signal_avg ?: linkstat.data.signal;

	/* See comments in iw_cache_update */
	if (sig_level == 0)
		sig_level = linkstat.data.bss_signal;

	if (linkstat.data.bss_signal_qual) {
		/* BSS_SIGNAL_UNSPEC is scaled 0..100 */
		sig_qual     = linkstat.data.bss_signal_qual;
		sig_qual_max = 100;
	} else if (sig_level) {
		if (sig_level < -110)
			sig_qual = 0;
		else if (sig_level > -40)
			sig_qual = 70;
		else
			sig_qual = sig_level + 110;
		sig_qual_max = 70;
	}

	if (sig_qual == -1) {
		p_stat->link_quality = -1;
		p_stat->link_quality_max = -1;
	} else {
		qual = ewma(qual, sig_qual, conf.meter_decay / 100.0);
		p_stat->link_quality = (int)qual;
		p_stat->link_quality_max = sig_qual_max;
	}

	if (sig_level != 0) {
		signal = ewma(signal, sig_level, conf.meter_decay / 100.0);
		p_stat->signal_level = (int)signal;
	} else {
		p_stat->signal_level = -1;
	}

	p_stat->signal_level_max = conf.sig_max;
	p_stat->signal_level_min = conf.sig_min;

	if (noise_data_valid) {
		noise = ewma(noise, linkstat.data.survey.noise, conf.meter_decay / 100.0);
		p_stat->noise_level = (int)noise;
	} else {
		p_stat->noise_level = 0;
	}

	p_stat->noise_level_max = conf.noise_max;
	p_stat->noise_level_min = conf.noise_min;

	if (noise_data_valid && sig_level) {
		ssnr = ewma(ssnr, sig_level - linkstat.data.survey.noise,
				  conf.meter_decay / 100.0);

		p_stat->ssnr = (int)ssnr;
	} else {
		p_stat->ssnr = -1;
	}
}

static void display_stats(void)
{
	char tmp[0x100];

	/*
	 * Interface RX stats
	 */
	mvwaddstr(w_stats, 1, 1, "RX: ");

	if (linkstat.data.rx_packets) {
		sprintf(tmp, "%'u (%s)",  linkstat.data.rx_packets,
			byte_units(linkstat.data.rx_bytes));
		waddstr_b(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (iw_nl80211_have_survey_data(&linkstat.data)) {
		if (linkstat.data.rx_bitrate[0]) {
			waddstr(w_stats, ", rate: ");
			waddstr_b(w_stats, linkstat.data.rx_bitrate);
		}

		if (linkstat.data.expected_thru) {
			if (linkstat.data.expected_thru >= 1024)
				sprintf(tmp, " (expected: %.1f MB/s)",  linkstat.data.expected_thru/1024.0);
			else
				sprintf(tmp, " (expected: %u kB/s)",  linkstat.data.expected_thru);
			waddstr(w_stats, tmp);
		}
	}

	if (linkstat.data.rx_drop_misc) {
		waddstr(w_stats, ", drop: ");
		sprintf(tmp, "%'llu (%.1f%%)", (unsigned long long)linkstat.data.rx_drop_misc,
				(1e2 * linkstat.data.rx_drop_misc)/linkstat.data.rx_packets);
		waddstr_b(w_stats, tmp);
	}

	wclrtoborder(w_stats);

	/*
	 * Interface TX stats
	 */
	mvwaddstr(w_stats, 2, 1, "TX: ");

	if (linkstat.data.tx_packets) {
		sprintf(tmp, "%'u (%s)",  linkstat.data.tx_packets,
			byte_units(linkstat.data.tx_bytes));
		waddstr_b(w_stats, tmp);
	} else {
		waddstr(w_stats, "n/a");
	}

	if (iw_nl80211_have_survey_data(&linkstat.data) && linkstat.data.tx_bitrate[0]) {
		waddstr(w_stats, ", rate: ");
		waddstr_b(w_stats, linkstat.data.tx_bitrate);
	}

	if (linkstat.data.tx_retries) {
		waddstr(w_stats, ", retries: ");
		sprintf(tmp, "%'u (%.1f%%)", linkstat.data.tx_retries,
			(1e2 * linkstat.data.tx_retries)/linkstat.data.tx_packets);
		waddstr_b(w_stats, tmp);
	}

	if (linkstat.data.tx_failed) {
		waddstr(w_stats, ", failed: ");
		sprintf(tmp, "%'u", linkstat.data.tx_failed);
		waddstr_b(w_stats, tmp);
	}
	wclrtoborder(w_stats);
	wrefresh(w_stats);
}

void print_wifi_statistics(wifi_statistics *p_stat)
{
	p_stat->inactive_time = linkstat.data.inactive_time;
	p_stat->connected_time = linkstat.data.connected_time;
	p_stat->rx_packets = linkstat.data.rx_packets;
	p_stat->rx_bytes = linkstat.data.rx_bytes;

	p_stat->rx_bitrate[0] = 0;
	p_stat->tx_bitrate[0] = 0;
	if (iw_nl80211_have_survey_data(&linkstat.data)) {
		if (linkstat.data.rx_bitrate[0]) {
			sprintf(p_stat->rx_bitrate, "%s", linkstat.data.rx_bitrate);
		}
		if (linkstat.data.tx_bitrate[0]) {
			sprintf(p_stat->tx_bitrate, "%s", linkstat.data.tx_bitrate);
		}
	}

	p_stat->rx_drop_misc = linkstat.data.rx_drop_misc;

	/*
	 * Interface TX stats
	 */

	p_stat->tx_packets = linkstat.data.tx_packets;
	p_stat->tx_bytes = linkstat.data.tx_bytes;

	p_stat->tx_retries = linkstat.data.tx_retries;
	p_stat->tx_failed = linkstat.data.tx_failed;
}

static void display_info(WINDOW *w_if, WINDOW *w_info)
{
	struct iw_dyn_info info;
	struct iw_range	range;
	struct iw_nl80211_ifstat ifs;
	struct iw_nl80211_reg ir;
	char tmp[0x100];

	iw_getinf_range(conf_ifname(), &range);
	dyn_info_get(&info, conf_ifname());
	iw_nl80211_getifstat(&ifs);
	iw_nl80211_getreg(&ir);

	/*
	 * Interface Part
	 */
	wmove(w_if, 1, 1);
	waddstr_b(w_if, conf_ifname());
	sprintf(tmp, " (%s)", info.name);
	waddstr(w_if, tmp);

	/* PHY */
	waddstr(w_if, ", phy ");
	sprintf(tmp, "%d", ifs.phy);
	waddstr_b(w_if, tmp);

	/* Regulatory domain */
	waddstr(w_if, ", reg: ");
	if (ir.region > 0) {
		waddstr_b(w_if, ir.country);
		sprintf(tmp, " (%s)", dfs_domain_name(ir.region));
		waddstr(w_if, tmp);
	} else {
		waddstr_b(w_if, "n/a");
	}

	if (ifs.ssid[0]) {
		waddstr(w_if, ", SSID: ");
		waddstr_b(w_if, ifs.ssid);
	}

	wclrtoborder(w_if);
	wrefresh(w_if);

	/*
	 * Info window:
	 */
	wmove(w_info, 1, 1);
	waddstr(w_info, "mode: ");
	waddstr_b(w_info, iftype_name(ifs.iftype));

	if (!ether_addr_is_zero(&linkstat.data.bssid)) {
		waddstr_b(w_info, ", ");

		switch (linkstat.data.status) {
		case NL80211_BSS_STATUS_ASSOCIATED:
			waddstr(w_info, "connected to: ");
			break;
		case NL80211_BSS_STATUS_AUTHENTICATED:
			waddstr(w_info, "authenticated with: ");
			break;
		case NL80211_BSS_STATUS_IBSS_JOINED:
			waddstr(w_info, "joined IBSS: ");
			break;
		default:
			waddstr(w_info, "station: ");
		}
		waddstr_b(w_info, ether_lookup(&linkstat.data.bssid));

		if (linkstat.data.status == NL80211_BSS_STATUS_ASSOCIATED) {
			waddstr_b(w_info, ",");
			waddstr(w_info, " time: ");
			waddstr_b(w_info, pretty_time(linkstat.data.connected_time));

			waddstr(w_info, ", inactive: ");
			sprintf(tmp, "%.1fs", (float)linkstat.data.inactive_time/1e3);
			waddstr_b(w_info, tmp);
		}
	}
	wclrtoborder(w_info);

	wmove(w_info, 2, 1);
	/* Frequency / channel */
	if (ifs.freq) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%d MHz", ifs.freq);
		waddstr_b(w_info, tmp);

		/* The following condition should in theory never happen */
		if (linkstat.data.survey.freq && linkstat.data.survey.freq != ifs.freq) {
			sprintf(tmp, " [survey freq: %d MHz]", linkstat.data.survey.freq);
			waddstr(w_info, tmp);
		}

		if (ifs.freq_ctr1 && ifs.freq_ctr1 != ifs.freq) {
			waddstr(w_info, ", ctr1: ");
			sprintf(tmp, "%d MHz", ifs.freq_ctr1);
			waddstr_b(w_info, tmp);
		}
		if (ifs.freq_ctr2 && ifs.freq_ctr2 != ifs.freq_ctr1 && ifs.freq_ctr2 != ifs.freq) {
			waddstr(w_info, ", ctr2: ");
			sprintf(tmp, "%d MHz", ifs.freq_ctr2);
			waddstr_b(w_info, tmp);
		}

		waddstr(w_info, ", channel: ");
		sprintf(tmp, "%d", ieee80211_frequency_to_channel(ifs.freq));
		waddstr_b(w_info, tmp);

		if (ifs.chan_width >= 0) {
			sprintf(tmp, " (width: %s)", channel_width_name(ifs.chan_width));
			waddstr(w_info, tmp);
		} else if (ifs.chan_type >= 0) {
			sprintf(tmp, " (%s)", channel_type_name(ifs.chan_type));
			waddstr(w_info, tmp);
		}
	} else if (iw_nl80211_have_survey_data(&linkstat.data)) {
		waddstr(w_info, "freq: ");
		sprintf(tmp, "%d MHz", linkstat.data.survey.freq);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "frequency/channel: n/a");
	}
	wclrtoborder(w_info);

	/* Channel data */
	wmove(w_info, 3, 1);
	if (iw_nl80211_have_survey_data(&linkstat.data)) {
		waddstr(w_info, "channel ");
		waddstr(w_info, "active: ");
		waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.active));

		waddstr(w_info, ", busy: ");
		waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.busy));

		if (linkstat.data.survey.time.ext_busy) {
			waddstr(w_info, ", ext-busy: ");
			waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.ext_busy));
		}

		waddstr(w_info, ", rx: ");
		waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.rx));

		waddstr(w_info, ", tx: ");
		waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.tx));

		if (linkstat.data.survey.time.scan) {
			waddstr(w_info, ", scan: ");
			waddstr_b(w_info, pretty_time_ms(linkstat.data.survey.time.scan));
		}
	} else if (linkstat.data.tx_bitrate[0] && linkstat.data.rx_bitrate[0]) {
		waddstr(w_info, "rx rate: ");
		waddstr_b(w_info, linkstat.data.rx_bitrate);

		if (linkstat.data.expected_thru) {
			if (linkstat.data.expected_thru >= 1024)
				sprintf(tmp, " (exp: %.1f MB/s)",  linkstat.data.expected_thru/1024.0);
			else
				sprintf(tmp, " (exp: %u kB/s)",  linkstat.data.expected_thru);
			waddstr(w_info, tmp);
		}
		waddstr(w_info, ", tx rate: ");
		waddstr_b(w_info, linkstat.data.tx_bitrate);
	}

	/* Beacons */
	wmove(w_info, 4, 1);

	if (linkstat.data.beacons) {
		waddstr(w_info, "beacons: ");
		sprintf(tmp, "%'llu", (unsigned long long)linkstat.data.beacons);
		waddstr_b(w_info, tmp);

		if (linkstat.data.beacon_loss) {
			waddstr(w_info, ", lost: ");
			sprintf(tmp, "%'u", linkstat.data.beacon_loss);
			waddstr_b(w_info, tmp);
		}
		waddstr(w_info, ", avg sig: ");
		sprintf(tmp, "%d dBm", (int8_t)linkstat.data.beacon_avg_sig);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", interval: ");
		sprintf(tmp, "%.1fs", (linkstat.data.beacon_int * 1024.0)/1e6);
		waddstr_b(w_info, tmp);

		waddstr(w_info, ", DTIM: ");
		sprintf(tmp, "%u", linkstat.data.dtim_period);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "station flags:");
		if (linkstat.data.cts_protection)
			waddstr_b(w_info, " CTS");
		if (linkstat.data.wme)
			waddstr_b(w_info, " WME");
		if (linkstat.data.tdls)
			waddstr_b(w_info, " TDLS");
		if (linkstat.data.mfp)
			waddstr_b(w_info, " MFP");
		if (!(linkstat.data.cts_protection | linkstat.data.wme | linkstat.data.tdls | linkstat.data.mfp))
			waddstr_b(w_info, " (none)");
		waddstr(w_info, ", preamble:");
		if (linkstat.data.long_preamble)
			waddstr_b(w_info, " long");
		else
			waddstr_b(w_info, " short");
		waddstr(w_info, ", slot:");
		if (linkstat.data.short_slot_time)
			waddstr_b(w_info, " short");
		else
			waddstr_b(w_info, " long");
	}

	if (info.cap_sens) {
		waddstr(w_info, ",  sensitivity: ");
		if (info.sens < 0)
			sprintf(tmp, "%d dBm", info.sens);
		else
			sprintf(tmp, "%d/%d", info.sens,
				range.sensitivity);
		waddstr_b(w_info, tmp);
	}

	wclrtoborder(w_info);

	wmove(w_info, 5, 1);
	waddstr(w_info, "power mgt: ");
	if (info.cap_power)
		waddstr_b(w_info, format_power(&info.power, &range));
	else
		waddstr(w_info, "n/a");

	if (info.cap_txpower && info.txpower.disabled) {
		waddstr(w_info, ",  tx-power: off");
	} else if (info.cap_txpower) {
		/*
		 * Convention: auto-selected values start with a capital
		 *             letter, otherwise with a small letter.
		 */
		if (info.txpower.fixed)
			waddstr(w_info, ",  tx-power: ");
		else
			waddstr(w_info, ",  TX-power: ");
		waddstr_b(w_info, format_txpower(&info.txpower));
	}
	wclrtoborder(w_info);

	wmove(w_info, 6, 1);
	waddstr(w_info, "retry: ");
	if (info.cap_retry)
		waddstr_b(w_info, format_retry(&info.retry, &range));
	else
		waddstr(w_info, "n/a");

	waddstr(w_info, ",  ");
	if (info.cap_rts) {
		waddstr(w_info, info.rts.fixed ? "rts/cts: " : "RTS/cts: ");
		if (info.rts.disabled)
			sprintf(tmp, "off");
		else
			sprintf(tmp, "%d B", info.rts.value);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "rts/cts: n/a");
	}

	waddstr(w_info, ",  ");
	if (info.cap_frag) {
		waddstr(w_info, info.frag.fixed ? "frag: " : "Frag: ");
		if (info.frag.disabled)
			sprintf(tmp, "off");
		else
			sprintf(tmp, "%d B", info.frag.value);
		waddstr_b(w_info, tmp);
	} else {
		waddstr(w_info, "frag: n/a");
	}
	wclrtoborder(w_info);

	/* FIXME: re-enable encryption information (issue #8)
	wmove(w_info, 7, 1);
	waddstr(w_info, "encryption: ");
	*/

	wclrtoborder(w_info);
	wrefresh(w_info);
}

void print_info(wifi_stat *p_stat) {
	struct iw_dyn_info info;
	struct iw_range	range;
	struct iw_nl80211_ifstat ifs;
	struct iw_nl80211_reg ir;

	iw_getinf_range(conf_ifname(), &range);
	dyn_info_get(&info, conf_ifname());
	iw_nl80211_getifstat(&ifs);
	iw_nl80211_getreg(&ir);

	/*
	 * Interface Part
	 */
	sprintf(p_stat->interface, "%s", conf_ifname());
	sprintf(p_stat->protocol, "%s", info.name);

	/* PHY */
	p_stat->phy = ifs.phy;

	/* Regulatory domain */
	if (ir.region > 0) {
		sprintf(p_stat->reg, "%s", dfs_domain_name(ir.region));
	} else {
		p_stat->reg[0] = 0;
	}

	if (ifs.ssid[0]) {
		sprintf(p_stat->ssid, "%s", ifs.ssid);
	} else {
		p_stat->ssid[0] = 0;
	}

	/*
	 * Info:
	 */
	p_stat->mode[0] = 0;
	sprintf(p_stat->mode, "%s", iftype_name(ifs.iftype));

	if (!ether_addr_is_zero(&linkstat.data.bssid)) {

		switch (linkstat.data.status) {
		case NL80211_BSS_STATUS_ASSOCIATED:
			sprintf(p_stat->bss_status, "%s", "connected to: ");
			break;
		case NL80211_BSS_STATUS_AUTHENTICATED:
			sprintf(p_stat->bss_status, "%s", "authenticated with: ");
			break;
		case NL80211_BSS_STATUS_IBSS_JOINED:
			sprintf(p_stat->bss_status, "%s", "joined IBSS: ");
			break;
		default:
			sprintf(p_stat->bss_status, "%s", "station: ");
		}
		sprintf(p_stat->bss_info, "%s", ether_lookup(&linkstat.data.bssid));
	}
	
	/* Frequency / channel */
	if (ifs.freq) {
		p_stat->freq = ifs.freq;

		/* The following condition should in theory never happen */
		if (linkstat.data.survey.freq && linkstat.data.survey.freq != ifs.freq) {
			p_stat->survey_freq = linkstat.data.survey.freq;
		}

		if (ifs.freq_ctr1 && ifs.freq_ctr1 != ifs.freq) {
			p_stat->freq_ctr1 = ifs.freq_ctr1;
		}
		if (ifs.freq_ctr2 && ifs.freq_ctr2 != ifs.freq_ctr1 && ifs.freq_ctr2 != ifs.freq) {
			p_stat->freq_ctr2 = ifs.freq_ctr2;
		}

		p_stat->channel = ieee80211_frequency_to_channel(ifs.freq);

		if (ifs.chan_width >= 0) {
			sprintf(p_stat->channel_width_name, "%s", channel_width_name(ifs.chan_width));
		} else if (ifs.chan_type >= 0) {
			sprintf(p_stat->channel_type_name, "%s", channel_type_name(ifs.chan_type));
		}
	} else if (iw_nl80211_have_survey_data(&linkstat.data)) {
		p_stat->freq = linkstat.data.survey.freq;
	} else {
		p_stat->channel = -1;
		p_stat->freq = -1;
	}

	/* Channel data */
	if (iw_nl80211_have_survey_data(&linkstat.data)) {
		p_stat->channel_survey_active = linkstat.data.survey.time.active;
		p_stat->channel_survey_busy = linkstat.data.survey.time.busy;

		if (linkstat.data.survey.time.ext_busy) {
			p_stat->channel_survey_ext_busy = linkstat.data.survey.time.ext_busy;
		}

		p_stat->channel_survey_rx = linkstat.data.survey.time.rx;

		p_stat->channel_survey_tx = linkstat.data.survey.time.tx;

		if (linkstat.data.survey.time.scan) {
			p_stat->channel_survey_scan = linkstat.data.survey.time.scan;
		}
	} else if (linkstat.data.tx_bitrate[0] && linkstat.data.rx_bitrate[0]) {
		sprintf(p_stat->channel_rx_bitrate, "%s", linkstat.data.rx_bitrate);

		p_stat->channel_expected_thru = linkstat.data.expected_thru;

		sprintf(p_stat->channel_tx_bitrate, "%s", linkstat.data.tx_bitrate);
	}

	if (linkstat.data.beacons) {
		p_stat->beacons = linkstat.data.beacons;

		p_stat->beacon_loss = linkstat.data.beacon_loss;

		p_stat->beacon_avg_sig = linkstat.data.beacon_avg_sig;

		p_stat->beacon_interval = (linkstat.data.beacon_int * 1024.0)/1e6;

		p_stat->beacon_dtim_period = linkstat.data.dtim_period;
	} else {
		p_stat->flag_cts_protection = linkstat.data.cts_protection;
		p_stat->flag_wme = linkstat.data.wme;
		p_stat->flag_tdls = linkstat.data.tdls;
		p_stat->flag_mfp = linkstat.data.mfp;
		
		p_stat->long_preamble = linkstat.data.long_preamble;
		p_stat->short_slot_time = linkstat.data.short_slot_time;
	}

	if (info.cap_sens) {
		p_stat->sensitivity = info.sens;
		p_stat->sensitivity_range = range.sensitivity;
	}

	if (info.cap_power)
		sprintf(p_stat->cap_power, "%s", format_power(&info.power, &range));
	else
		sprintf(p_stat->cap_power, "%s", "n/a");

	if (info.cap_txpower && info.txpower.disabled) {
		sprintf(p_stat->cap_txpower, "%s", "off");
	} else if (info.cap_txpower) {
		sprintf(p_stat->cap_txpower, "%s", format_txpower(&info.txpower));
	}

	if (info.cap_retry)
		sprintf(p_stat->cap_retry, "%s", format_retry(&info.retry, &range));
	else
		sprintf(p_stat->cap_retry, "%s", "n/a");

	p_stat->cap_rts = 0;
	if (info.cap_rts) {
		if (info.rts.disabled)
			p_stat->cap_rts_off = 1;
		else
			p_stat->cap_rts = info.rts.value;
	} else {
			p_stat->cap_rts = 0;
	}

	p_stat->cap_frag = 0;
	if (info.cap_frag) {
		if (info.frag.disabled)
			p_stat->cap_frag_off = 1;
		else
			p_stat->cap_frag = info.frag.value;
	} else {
		p_stat->cap_frag = 0;
	}

}

static void display_netinfo(WINDOW *w_net)
{
	struct if_info info;
	char tmp[0x40];

	if_getinf(conf_ifname(), &info);

	wmove(w_net, 1, 1);
	wclrtoborder(w_net);
	if (getmaxy(w_net) == WH_NET_MAX) {
		waddstr(w_net, conf_ifname());

		waddstr_b(w_net, " (");
		waddstr(w_net, info.flags & IFF_UP ? "UP" : "DOWN");
		if (info.flags & IFF_RUNNING)		/* Interface RFC2863 OPER_UP	*/
			waddstr(w_net, " RUNNING");
#ifdef IFF_LOWER_UP	/* Linux 2.6.17 */
		if (info.flags & IFF_LOWER_UP)		/* Driver signals L1 up		*/
			waddstr(w_net, " LOWER_UP");
#endif
#ifdef IFF_DORMANT	/* Linux 2.6.17 */
		if (info.flags & IFF_DORMANT)		/* Driver signals dormant	*/
			waddstr(w_net, " DORMANT");
#endif
		if (info.flags & IFF_MASTER)		/* Master of a load balancer 	*/
			waddstr(w_net, " MASTER");
		if (info.flags & IFF_SLAVE)		/* Slave of a load balancer 	*/
			waddstr(w_net, " SLAVE");
		if (info.flags & IFF_POINTOPOINT)	/* Is a point-to-point link	*/
			waddstr(w_net, " POINTOPOINT");
		if (info.flags & IFF_DYNAMIC)		/* Address is volatile		*/
			waddstr(w_net, " DYNAMIC");
		if (info.flags & IFF_BROADCAST)		/* Valid broadcast address set	*/
			waddstr(w_net, " BROADCAST");
		if (info.flags & IFF_MULTICAST)		/* Supports multicast		*/
			waddstr(w_net, " MULTICAST");
		if (info.flags & IFF_ALLMULTI)		/* Receive all mcast  packets	*/
			waddstr(w_net, " ALLMULTI");
		if (info.flags & IFF_NOARP)		/* No ARP protocol		*/
			waddstr(w_net, " NOARP");
		if (info.flags & IFF_NOTRAILERS)	/* Avoid use of trailers	*/
			waddstr(w_net, " NOTRAILERS");
		if (info.flags & IFF_PROMISC)		/* Is in promiscuous mode	*/
			waddstr(w_net, " PROMISC");
		if (info.flags & IFF_DEBUG)		/* Internal debugging flag	*/
			waddstr(w_net, " DEBUG");
		waddstr_b(w_net, ")");

		wmove(w_net, 2, 1);
		wclrtoborder(w_net);
	}
	waddstr(w_net, "mac: ");
	waddstr_b(w_net, ether_lookup(&info.hwaddr));

	if (getmaxy(w_net) == WH_NET_MAX) {
		waddstr(w_net, ", qlen: ");
		sprintf(tmp, "%u", info.txqlen);
		waddstr_b(w_net, tmp);

		wmove(w_net, 3, 1);
		wclrtoborder(w_net);
	} else {
		waddstr(w_net, ", ");
	}
	waddstr(w_net, "ip: ");

	if (!info.addr.s_addr) {
		waddstr(w_net, "n/a");
	} else {
		sprintf(tmp, "%s/%u", inet_ntoa(info.addr),
				      prefix_len(&info.netmask));
		waddstr_b(w_net, tmp);

		/* only show bcast address if not set to the obvious default */
		if (info.bcast.s_addr !=
		    (info.addr.s_addr | ~info.netmask.s_addr)) {
			waddstr(w_net, ",  bcast: ");
			waddstr_b(w_net, inet_ntoa(info.bcast));
		}
	}
	wclrtoborder(w_net);

	/* 802.11 MTU may be greater than Ethernet MTU (1500) */
	if (info.mtu && info.mtu != ETH_DATA_LEN) {
		waddstr(w_net, ",  mtu: ");
		sprintf(tmp, "%u", info.mtu);
		waddstr_b(w_net, tmp);
	}

	wrefresh(w_net);
}

void print_netinfo(wifi_stat *p_stat)
{
	struct if_info info;

	if_getinf(conf_ifname(), &info);


	p_stat->net_flags.up = info.flags & IFF_UP;
	p_stat->net_flags.running = (info.flags & IFF_RUNNING);		/* Interface RFC2863 OPER_UP	*/

	p_stat->net_flags.lower_up = 0;
#ifdef IFF_LOWER_UP	/* Linux 2.6.17 */
	p_stat->net_flags.lower_up = (info.flags & IFF_LOWER_UP);		/* Driver signals L1 up		*/
#endif

	p_stat->net_flags.dormant = 0;
#ifdef IFF_DORMANT	/* Linux 2.6.17 */
	p_stat->net_flags.dormant = (info.flags & IFF_DORMANT);		/* Driver signals dormant	*/
#endif
	p_stat->net_flags.master = (info.flags & IFF_MASTER);		/* Master of a load balancer 	*/
	p_stat->net_flags.slave = (info.flags & IFF_SLAVE);		/* Slave of a load balancer 	*/
	p_stat->net_flags.point_to_point = (info.flags & IFF_POINTOPOINT);	/* Is a point-to-point link	*/
	p_stat->net_flags.dynamic = (info.flags & IFF_DYNAMIC);		/* Address is volatile		*/
	p_stat->net_flags.broadcast = (info.flags & IFF_BROADCAST);		/* Valid broadcast address set	*/
	p_stat->net_flags.multicast = (info.flags & IFF_MULTICAST);		/* Supports multicast		*/
	p_stat->net_flags.all_multi = (info.flags & IFF_ALLMULTI);		/* Receive all mcast  packets	*/
	p_stat->net_flags.no_arp = (info.flags & IFF_NOARP);		/* No ARP protocol		*/
	p_stat->net_flags.no_trailers = (info.flags & IFF_NOTRAILERS);	/* Avoid use of trailers	*/
	p_stat->net_flags.promiscuous = (info.flags & IFF_PROMISC);		/* Is in promiscuous mode	*/
	p_stat->net_flags.debugging = (info.flags & IFF_DEBUG);		/* Internal debugging flag	*/

	sprintf(p_stat->mac, "%s", ether_lookup(&info.hwaddr));

	if (getmaxy(w_net) == WH_NET_MAX) {
		p_stat->txqlen = info.txqlen;
	} else {
		p_stat->txqlen = 0;
	}

	if (!info.addr.s_addr) {
		p_stat->ip[0] = 0;
	} else {
		sprintf(p_stat->ip, "%s", inet_ntoa(info.addr));

		/* only show bcast address if not set to the obvious default */
		if (info.bcast.s_addr !=
		    (info.addr.s_addr | ~info.netmask.s_addr)) {
			sprintf(p_stat->bcast_ip, "%s", inet_ntoa(info.bcast));
		} else {
			p_stat->bcast_ip[0] = 0;
		}
	}

	/* 802.11 MTU may be greater than Ethernet MTU (1500) */
	if (info.mtu && info.mtu != ETH_DATA_LEN) {
		p_stat->mtu = info.mtu;
	} else {
		p_stat->mtu = 0;
	}
}

void scr_info_init(void)
{
	int line = 0;

	w_if	 = newwin_title(line, WH_IFACE, "Interface", true);
	line += WH_IFACE;
	w_levels = newwin_title(line, WH_LEVEL, "Levels", true);
	line += WH_LEVEL;
	w_stats	 = newwin_title(line, WH_STATS, "Statistics", true);
	line += WH_STATS;
	w_info	 = newwin_title(line, WH_INFO_MIN, "Info", true);
	line += WH_INFO_MIN;
	if (LINES >= WH_INFO_SCR_MIN + (WH_NET_MAX - WH_NET_MIN))
		w_net = newwin_title(line, WH_NET_MAX, "Network", false);
	else
		w_net = newwin_title(line, WH_NET_MAX, "Network", false);

	sampling_init();
}

int scr_info_loop(WINDOW *w_menu)
{
	time_t now = time(NULL);

	if (!pthread_mutex_trylock(&linkstat.mutex)) {
		display_levels();
		display_stats();
		pthread_mutex_unlock(&linkstat.mutex);
	}

	if (now - last_update >= conf.info_iv) {
		last_update = now;
		display_info(w_if, w_info);
		display_netinfo(w_net);
	}
	return wgetch(w_menu);
}

void scr_info_fini(void)
{
	sampling_stop();

	delwin(w_net);
	delwin(w_info);
	delwin(w_stats);
	delwin(w_levels);
	delwin(w_if);
}

void get_wifi_stat(wifi_stat *p_stat)
{
	iw_nl80211_get_linkstat(&linkstat.data);

	print_info(p_stat);

	print_netinfo(p_stat);

	print_wifi_levels(&(p_stat->level));

	print_wifi_statistics(&(p_stat->statistics));
}
