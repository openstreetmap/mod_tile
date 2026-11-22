/*
 * Copyright (c) 2007 - 2026 by mod_tile contributors (see AUTHORS file)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see http://www.gnu.org/licenses/.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/un.h>
#include <unistd.h>

#include "config.h"
#include "g_logger.h"
#include "protocol.h"
#include "render_config.h"
#include "renderd.h"

#ifdef HAVE_INIPARSER_INIPARSER_H
#include <iniparser/iniparser.h>
#else
#include <iniparser.h>
#endif

/* Compatibility macros for iniparser < 4.0 (non-const API) vs >= 4.0 (const-correct API) */
#ifndef INIPARSER_USES_CONST
/** @brief Cast a `const dictionary *` for legacy non-const iniparser functions. */
#define INIPARSER_DICT_CAST(d) ((dictionary *)(d))
/** @brief Cast a `const char *` for legacy non-const iniparser functions. */
#define INIPARSER_STR_CAST(s) ((char *)(s))
#else
/** @brief Pass a `const dictionary *` directly to const-correct iniparser functions. */
#define INIPARSER_DICT_CAST(d) (d)
/** @brief Pass a `const char *` directly to const-correct iniparser functions. */
#define INIPARSER_STR_CAST(s) (s)
#endif

/** @brief Total number of map style sections parsed into `maps[]`. */
int num_maps = 0;
/** @brief Sum of `num_threads` across all inactive (slave) `[renderdN]` sections. */
int num_slave_threads = 0;
/** @brief Pointer to the active `renderd_config` entry inside `config_slaves[]`. */
renderd_config *config;
/** @brief Array of parsed `[renderd]` / `[renderdN]` instance configurations indexed by section number. */
renderd_config config_slaves[MAX_SLAVES];
/** @brief Array of parsed map style configurations (`[default]`, etc.) up to `XMLCONFIGS_MAX`. */
xmlconfigitem maps[XMLCONFIGS_MAX];

/**
 * @brief Duplicate up to `maxlen` bytes of `src` onto the heap and store in `*dest`.
 *
 * Logs a critical error and terminates with exit code `7` if `strndup()` fails.
 *
 * @param src    Source null-terminated string to copy.
 * @param dest   Output pointer receiving the newly allocated string.
 * @param maxlen Maximum number of characters to copy from `src`.
 */
static void copy_string(const char *src, const char **dest, size_t maxlen)
{
	*dest = strndup(src, strnlen(src, maxlen));

	if (*dest == NULL) {
		g_logger(G_LOG_LEVEL_CRITICAL, "copy_string: strndup error");
		exit(7);
	}
}

/**
 * @brief Format an iniparser lookup key as `"<section>:<name>"`.
 *
 * Allocates the combined key string on the heap (caller must `free()`). Exits with
 * code `7` if `section` or `name` is `NULL` or if `asprintf()` fails.
 *
 * @param section INI section name (e.g., `"renderd"`).
 * @param name    INI key name within the section (e.g., `"num_threads"`).
 * @return Heap-allocated `"section:name"` key string.
 */
static char *name_with_section(const char *section, const char *name)
{
	/* Byte length returned by asprintf(), or -1 on allocation failure */
	int len;
	/* Heap-allocated formatted "section:name" lookup key */
	char *key;

	if (!section) {
		g_logger(G_LOG_LEVEL_CRITICAL, "name_with_section: invalid section %s", section);
		exit(7);
	}

	if (!name) {
		g_logger(G_LOG_LEVEL_CRITICAL, "name_with_section: invalid name %s", name);
		exit(7);
	}

	len = asprintf(&key, "%s:%s", section, name);

	if (len == -1) {
		g_logger(G_LOG_LEVEL_CRITICAL, "name_with_section: asprintf error");
		exit(7);
	}

	return key;
}

/**
 * @brief Read a boolean configuration value from an INI dictionary.
 *
 * @param ini      Parsed iniparser dictionary.
 * @param section  INI section name.
 * @param name     INI option name.
 * @param dest     Output integer receiving `1` (true) or `0` (false).
 * @param notfound Default boolean value returned if the key is absent.
 */
static void process_config_bool(const dictionary *ini, const char *section, const char *name, int *dest, int notfound)
{
	/* Formatted "section:name" iniparser lookup key */
	char *key = name_with_section(section, name);
	/* Parsed boolean result (1 or 0) */
	int src = iniparser_getboolean(INIPARSER_DICT_CAST(ini), key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src ? "true" : "false");

	*dest = src;

	free(key);
}

/**
 * @brief Read a floating-point (`double`) configuration value from an INI dictionary.
 *
 * @param ini      Parsed iniparser dictionary.
 * @param section  INI section name.
 * @param name     INI option name.
 * @param dest     Output double receiving the parsed value.
 * @param notfound Default double value returned if the key is absent.
 */
static void process_config_double(const dictionary *ini, const char *section, const char *name, double *dest, double notfound)
{
	/* Formatted "section:name" iniparser lookup key */
	char *key = name_with_section(section, name);
	/* Parsed double-precision value */
	double src = iniparser_getdouble(INIPARSER_DICT_CAST(ini), key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%lf'", key, src);

	*dest = src;

	free(key);
}

/**
 * @brief Read an integer configuration value from an INI dictionary.
 *
 * @param ini      Parsed iniparser dictionary.
 * @param section  INI section name.
 * @param name     INI option name.
 * @param dest     Output integer receiving the parsed value.
 * @param notfound Default integer value returned if the key is absent.
 */
static void process_config_int(const dictionary *ini, const char *section, const char *name, int *dest, int notfound)
{
	/* Formatted "section:name" iniparser lookup key */
	char *key = name_with_section(section, name);
	/* Parsed integer value */
	int src = iniparser_getint(INIPARSER_DICT_CAST(ini), key, notfound);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%i'", key, src);

	*dest = src;

	free(key);
}

/**
 * @brief Read a string configuration value from an INI dictionary and duplicate it onto the heap.
 *
 * @param ini      Parsed iniparser dictionary.
 * @param section  INI section name.
 * @param name     INI option name.
 * @param dest     Output pointer receiving the heap-allocated copy of the string.
 * @param notfound Default fallback string if the key is absent.
 * @param maxlen   Maximum byte length to copy via copy_string().
 */
static void process_config_string(const dictionary *ini, const char *section, const char *name, const char **dest, const char *notfound, size_t maxlen)
{
	/* Formatted "section:name" iniparser lookup key */
	char *key = name_with_section(section, name);
	/* Raw string pointer owned by the iniparser dictionary */
	const char *src = iniparser_getstring(INIPARSER_DICT_CAST(ini), key, INIPARSER_STR_CAST(notfound));

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src);

	copy_string(src, dest, maxlen);

	free(key);
}

/**
 * @brief Read a string configuration value and guarantee it ends with a trailing `'/'`.
 *
 * Used for URI path prefixes (`uri`) so tile paths can be concatenated directly.
 * If the configured value is empty or does not end with `'/'`, appends `'/'` before
 * copying to `*dest`.
 *
 * @param ini      Parsed iniparser dictionary.
 * @param section  INI section name.
 * @param name     INI option name.
 * @param dest     Output pointer receiving the heap-allocated slash-terminated string.
 * @param notfound Default fallback string if the key is absent.
 * @param maxlen   Maximum byte length to copy via copy_string().
 */
static void process_config_string_with_trailing_slash(const dictionary *ini, const char *section, const char *name, const char **dest, const char *notfound, size_t maxlen)
{
	/* Formatted "section:name" iniparser lookup key */
	char *key = name_with_section(section, name);
	/* Raw string pointer owned by the iniparser dictionary */
	const char *src = iniparser_getstring(INIPARSER_DICT_CAST(ini), key, INIPARSER_STR_CAST(notfound));
	/* Bounded byte length of src */
	size_t src_strlen = strnlen(src, maxlen);

	g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s: '%s'", key, src);

	/* An empty value also gets a slash; checking src[src_strlen - 1] directly would read out of bounds */
	if (src_strlen == 0 || src[src_strlen - 1] != '/') {
		/* Temporary buffer holding `src` with an appended '/' */
		char *tempsrc;
		/* Byte length returned by asprintf(), or -1 on error */
		int len = asprintf(&tempsrc, "%s/", src);

		if (len == -1) {
			g_logger(G_LOG_LEVEL_CRITICAL, "process_config_string_with_trailing_slash: asprintf error");
			exit(7);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\t\tAdded trailing slash to %s: '%s'", key, tempsrc);

		copy_string(tempsrc, dest, maxlen);

		free(tempsrc);
	} else {
		copy_string(src, dest, maxlen);
	}

	free(key);
}

/**
 * @brief Free all heap-allocated string fields inside a single `xmlconfigitem` and zero the struct.
 *
 * @param map_section Pointer to the `xmlconfigitem` structure to release.
 */
void free_map_section(xmlconfigitem *map_section)
{
	free((void *)map_section->attribution);
	free((void *)map_section->cors);
	free((void *)map_section->description);
	free((void *)map_section->file_extension);
	free((void *)map_section->host);
	free((void *)map_section->htcpip);
	free((void *)map_section->mime_type);
	free((void *)map_section->output_format);
	free((void *)map_section->parameterization);
	free((void *)map_section->server_alias);
	free((void *)map_section->tile_dir);
	free((void *)map_section->xmlfile);
	free((void *)map_section->xmlname);
	free((void *)map_section->xmluri);
	bzero(map_section, sizeof(xmlconfigitem));
}

/**
 * @brief Free all populated entries in an array of `XMLCONFIGS_MAX` map configurations.
 *
 * @param map_sections Array of `xmlconfigitem` structures (`XMLCONFIGS_MAX` elements).
 */
void free_map_sections(xmlconfigitem *map_sections)
{
	for (int i = 0; i < XMLCONFIGS_MAX; i++) {
		if (map_sections[i].xmlname != NULL) {
			free_map_section(&map_sections[i]);
		}
	}
}

/**
 * @brief Free all heap-allocated string fields inside a single `renderd_config` and zero the struct.
 *
 * @param renderd_section Pointer to the `renderd_config` structure to release.
 */
void free_renderd_section(renderd_config *renderd_section)
{
	free((void *)renderd_section->iphostname);
	free((void *)renderd_section->mapnik_font_dir);
	free((void *)renderd_section->mapnik_plugins_dir);
	free((void *)renderd_section->name);
	free((void *)renderd_section->pid_filename);
	free((void *)renderd_section->socketname);
	free((void *)renderd_section->stats_filename);
	free((void *)renderd_section->tile_dir);
	bzero(renderd_section, sizeof(renderd_config));
}

/**
 * @brief Free all populated entries in an array of `MAX_SLAVES` renderd instance configurations.
 *
 * @param renderd_sections Array of `renderd_config` structures (`MAX_SLAVES` elements).
 */
void free_renderd_sections(renderd_config *renderd_sections)
{
	for (int i = 0; i < MAX_SLAVES; i++) {
		if (renderd_sections[i].num_threads != 0) {
			free_renderd_section(&renderd_sections[i]);
		}
	}
}

/**
 * @brief Parse and validate a `double` command-line or config option against optional bounds.
 *
 * Terminates the process with `exit(1)` if `opt_arg` cannot be parsed as a floating-point
 * number or violates `[minimum, maximum]` (where `-1` disables the corresponding bound check).
 *
 * @param opt_arg       Input string to parse as a `double`.
 * @param opt_type_name Descriptive option name used in error log messages.
 * @param minimum       Inclusive lower bound, or `-1` to skip lower-bound validation.
 * @param maximum       Inclusive upper bound, or `-1` to skip upper-bound validation.
 * @return Parsed and validated `double` value.
 */
double min_max_double_opt(const char *opt_arg, const char *opt_type_name, double minimum, double maximum)
{
	/* Pointer set by strtod() to the first unparsed character */
	char *endptr;
	/* Parsed floating-point option value */
	double opt = strtod(opt_arg, &endptr);

	if (endptr == opt_arg) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be a double (%s was provided)", opt_type_name, opt_arg);
		exit(1);
	} else if (minimum != -1 && opt < minimum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be >= %f (%s was provided)", opt_type_name, minimum, opt_arg);
		exit(1);
	} else if (maximum != -1 && opt > maximum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be <= %f (%s was provided)", opt_type_name, maximum, opt_arg);
		exit(1);
	}

	return opt;
}

/**
 * @brief Parse and validate an `int` command-line or config option against optional bounds.
 *
 * Rejects non-numeric strings as well as fractional numbers (by comparing `strtol()` and
 * `strtof()` results), and enforces `[minimum, maximum]` (where `-1` disables a bound check).
 * Terminates with `exit(1)` on invalid input.
 *
 * @param opt_arg       Input string to parse as an `int`.
 * @param opt_type_name Descriptive option name used in error log messages.
 * @param minimum       Inclusive lower bound, or `-1` to skip lower-bound validation.
 * @param maximum       Inclusive upper bound, or `-1` to skip upper-bound validation.
 * @return Parsed and validated integer value.
 */
int min_max_int_opt(const char *opt_arg, const char *opt_type_name, int minimum, int maximum)
{
	/* End pointers for integer and float parses (used to detect non-numeric or fractional inputs) */
	char *endptr, *endptr_float;
	/* Parsed base-10 integer value */
	int opt = strtol(opt_arg, &endptr, 10);
	/* Float parse used to verify that opt_arg had no fractional component */
	float opt_float = strtof(opt_arg, &endptr_float);

	if (endptr == opt_arg || endptr_float == opt_arg || (float)opt != opt_float) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be an integer (%s was provided)", opt_type_name, opt_arg);
		exit(1);
	} else if (minimum != -1 && opt < minimum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be >= %i (%s was provided)", opt_type_name, minimum, opt_arg);
		exit(1);
	} else if (maximum != -1 && opt > maximum) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Invalid %s, must be <= %i (%s was provided)", opt_type_name, maximum, opt_arg);
		exit(1);
	}

	return opt;
}

/**
 * @brief Parse all map style sections from a `renderd.conf` INI file into `maps_dest[]`.
 *
 * Treats every INI section whose name neither starts with `"renderd"` nor equals `"mapnik"`
 * as a map configuration section. Parses tile dimensions, zoom range (`0..MAX_ZOOM`), scale
 * factor (`0.1..8.0`), paths, HTCP settings, and space-delimited `type` (`<ext> <mime> [format]`),
 * then verifies that at least one map exists and no earlier map's `xmluri` is a prefix of a
 * later map's `xmluri`. Updates the global `num_maps` counter.
 *
 * @param ini              Pre-loaded iniparser dictionary, or `NULL` to load `config_file_name` locally.
 * @param config_file_name Path to the configuration file (used for loading if `ini == NULL` and in logs).
 * @param maps_dest        Destination array of `XMLCONFIGS_MAX` `xmlconfigitem` entries.
 * @param default_tile_dir Fallback tile directory inherited from the active `[renderd]` section.
 * @param num_threads      Active worker thread count propagated to each `xmlconfigitem`.
 */
void process_map_sections(const dictionary *ini, const char *config_file_name, xmlconfigitem *maps_dest, const char *default_tile_dir, int num_threads)
{
	/* Flag set to 1 if this function loaded `ini` itself and must free it before returning */
	int ini_loaded_here = 0;
	/* Zero-based index of the most recently parsed map section (-1 until first map is found) */
	int map_section_num = -1;

	if (!ini) {
		ini = iniparser_load(config_file_name);
		ini_loaded_here = 1;

		if (!ini) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file (process_map_sections): '%s'", config_file_name);
			exit(1);
		}
	}

	bzero(maps_dest, sizeof(xmlconfigitem) * XMLCONFIGS_MAX);

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(INIPARSER_DICT_CAST(ini)); section_num++) {
		/* Current INI section name */
		const char *section = iniparser_getsecname(INIPARSER_DICT_CAST(ini), section_num);

		if (strncmp(section, "renderd", 7) && strcmp(section, "mapnik")) { /* This is a map config section */
			/* Mutable copy of `type` setting and strtok_r() state pointer */
			char *ini_type_copy, *ini_type_context;
			/* Raw `type` config string (e.g., "png image/png png256") and current space-delimited token */
			const char *ini_type, *ini_type_part;
			/* Maximum token length and token index (0=ext, 1=mime, 2=Mapnik format) */
			int ini_type_part_maxlen = 64, ini_type_part_num = 0;

			map_section_num++;
			g_logger(G_LOG_LEVEL_DEBUG, "Parsing map config section %i: %s", map_section_num, section);

			if (map_section_num >= XMLCONFIGS_MAX) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i map config sections", XMLCONFIGS_MAX);
				exit(7);
			}

			copy_string(section, &maps_dest[map_section_num].xmlname, XMLCONFIG_MAX);

			process_config_int(ini, section, "aspectx", &maps_dest[map_section_num].aspect_x, 1);
			process_config_int(ini, section, "aspecty", &maps_dest[map_section_num].aspect_y, 1);
			process_config_int(ini, section, "tilesize", &maps_dest[map_section_num].tile_px_size, 256);
			process_config_string(ini, section, "attribution", &maps_dest[map_section_num].attribution, "", PATH_MAX);
			process_config_string(ini, section, "cors", &maps_dest[map_section_num].cors, "", PATH_MAX);
			process_config_string(ini, section, "description", &maps_dest[map_section_num].description, "", PATH_MAX);
			process_config_string(ini, section, "host", &maps_dest[map_section_num].host, "", PATH_MAX);
			process_config_string(ini, section, "htcphost", &maps_dest[map_section_num].htcpip, "", PATH_MAX);
			process_config_string(ini, section, "parameterize_style", &maps_dest[map_section_num].parameterization, "", PATH_MAX);
			process_config_string(ini, section, "server_alias", &maps_dest[map_section_num].server_alias, "", PATH_MAX);
			process_config_string(ini, section, "tiledir", &maps_dest[map_section_num].tile_dir, default_tile_dir, PATH_MAX);
			process_config_string(ini, section, "xml", &maps_dest[map_section_num].xmlfile, "", PATH_MAX);
			process_config_string_with_trailing_slash(ini, section, "uri", &maps_dest[map_section_num].xmluri, "", PATH_MAX);

			process_config_double(ini, section, "scale", &maps_dest[map_section_num].scale_factor, 1.0);

			if (maps_dest[map_section_num].scale_factor < 0.1) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too small, must be greater than or equal to %lf.", maps_dest[map_section_num].scale_factor, 0.1);
				exit(7);
			} else if (maps_dest[map_section_num].scale_factor > 8.0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified scale factor (%lf) is too large, must be less than or equal to %lf.", maps_dest[map_section_num].scale_factor, 8.0);
				exit(7);
			}

			process_config_int(ini, section, "maxzoom", &maps_dest[map_section_num].max_zoom, MAX_ZOOM);

			if (maps_dest[map_section_num].max_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too small, must be greater than or equal to %i.", maps_dest[map_section_num].max_zoom, 0);
				exit(7);
			} else if (maps_dest[map_section_num].max_zoom > MAX_ZOOM) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified max zoom (%i) is too large, must be less than or equal to %i.", maps_dest[map_section_num].max_zoom, MAX_ZOOM);
				exit(7);
			}

			process_config_int(ini, section, "minzoom", &maps_dest[map_section_num].min_zoom, 0);

			if (maps_dest[map_section_num].min_zoom < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is too small, must be greater than or equal to %i.", maps_dest[map_section_num].min_zoom, 0);
				exit(7);
			} else if (maps_dest[map_section_num].min_zoom > maps_dest[map_section_num].max_zoom) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified min zoom (%i) is larger than max zoom (%i).", maps_dest[map_section_num].min_zoom, maps_dest[map_section_num].max_zoom);
				exit(7);
			}

			process_config_string(ini, section, "type", &ini_type, "png image/png png256", INILINE_MAX);
			ini_type_copy = strndup(ini_type, INILINE_MAX);

			/* Split "type" into up to 3 space-separated tokens: <file_extension> <mime_type> [output_format] */
			for (ini_type_part = strtok_r(ini_type_copy, " ", &ini_type_context); ini_type_part; ini_type_part = strtok_r(NULL, " ", &ini_type_context)) {
				switch (ini_type_part_num) {
				case 0:
					copy_string(ini_type_part, &maps_dest[map_section_num].file_extension, ini_type_part_maxlen);
					break;

				case 1:
					copy_string(ini_type_part, &maps_dest[map_section_num].mime_type, ini_type_part_maxlen);
					break;

				case 2:
					copy_string(ini_type_part, &maps_dest[map_section_num].output_format, ini_type_part_maxlen);
					break;

				default:
					g_logger(G_LOG_LEVEL_CRITICAL, "Specified type (%s) has too many parts, there must be no more than 3, e.g., 'png image/png png256'.", ini_type);
					exit(7);
				}

				ini_type_part_num++;
			}

			if (ini_type_part_num < 2) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified type (%s) has too few parts, there must be at least 2, e.g., 'png image/png'.", ini_type);
				exit(7);
			}

			if (ini_type_part_num < 3) {
				/* Default Mapnik output encoding format when omitted from `type` */
				copy_string("png256", &maps_dest[map_section_num].output_format, ini_type_part_maxlen);
			}

			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:file_extension: '%s'", section, "type", maps_dest[map_section_num].file_extension);
			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:mime_type: '%s'", section, "type", maps_dest[map_section_num].mime_type);
			g_logger(G_LOG_LEVEL_DEBUG, "\tRead %s:%s:output_format: '%s'", section, "type", maps_dest[map_section_num].output_format);

			/* Pass thread count into each map config so rendering threads can size Mapnik DB connection pools */
			maps_dest[map_section_num].num_threads = num_threads;

			free(ini_type_copy);
			free((void *)ini_type);
		}
	}

	if (ini_loaded_here == 1) {
		iniparser_freedict((dictionary *)ini);
	}

	if (map_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No map config sections were found in file: %s", config_file_name);
		exit(1);
	}

	/* Ensure no earlier map's URI path is a prefix of a subsequent map's URI path (which would shadow it in Apache) */
	if (map_section_num > 0) {
		for (int x = 0; x < map_section_num; x++) {
			for (int y = x + 1; y <= map_section_num; y++) {
				if (strncmp(maps_dest[x].xmluri, maps_dest[y].xmluri, strnlen(maps_dest[x].xmluri, PATH_MAX)) == 0) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Specified URI path ('%s' in map section '%s') must not be the parent of any subsequent map config section's URI path, e.g., '%s' in map section '%s'.", maps_dest[x].xmluri, maps_dest[x].xmlname, maps_dest[y].xmluri, maps_dest[y].xmlname);
					exit(7);
				}
			}
		}
	}

	num_maps = map_section_num + 1;
}

/**
 * @brief Parse the `[mapnik]` section from a `renderd.conf` INI file into `config_dest`.
 *
 * Extracts `font_dir_recurse`, `font_dir`, and `plugins_dir`. Exits with code `1` if the
 * `[mapnik]` section is missing.
 *
 * @param ini              Pre-loaded iniparser dictionary, or `NULL` to load `config_file_name` locally.
 * @param config_file_name Path to the configuration file (used for loading if `ini == NULL` and in logs).
 * @param config_dest      Destination `renderd_config` structure receiving the Mapnik paths.
 */
void process_mapnik_section(const dictionary *ini, const char *config_file_name, renderd_config *config_dest)
{
	/* Flag set to 1 if this function loaded `ini` itself and must free it before returning */
	int ini_loaded_here = 0;
	/* Index of the `[mapnik]` section in `ini`, or -1 if not found */
	int mapnik_section_num = -1;

	if (!ini) {
		ini = iniparser_load(config_file_name);
		ini_loaded_here = 1;

		if (!ini) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file (process_mapnik_section): '%s'", config_file_name);
			exit(1);
		}
	}

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing mapnik config section");

	for (int section_num = 0; section_num < iniparser_getnsec(INIPARSER_DICT_CAST(ini)); section_num++) {
		/* Current INI section name */
		const char *section = iniparser_getsecname(INIPARSER_DICT_CAST(ini), section_num);

		if (strcmp(section, "mapnik") == 0) { /* This is the mapnik config section */
			mapnik_section_num = section_num;
			process_config_bool(ini, section, "font_dir_recurse", &config_dest->mapnik_font_dir_recurse, MAPNIK_FONTS_DIR_RECURSE);
			process_config_string(ini, section, "font_dir", &config_dest->mapnik_font_dir, MAPNIK_FONTS_DIR, PATH_MAX);
			process_config_string(ini, section, "plugins_dir", &config_dest->mapnik_plugins_dir, MAPNIK_PLUGINS_DIR, PATH_MAX);
			break;
		}
	}

	if (ini_loaded_here == 1) {
		iniparser_freedict((dictionary *)ini);
	}

	if (mapnik_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No mapnik config section was found in file: %s", config_file_name);
		exit(1);
	}
}

/**
 * @brief Parse all `[renderd]` and `[renderdN]` sections from a `renderd.conf` INI file.
 *
 * Maps `[renderd]` and `[renderd0]` to index `0`, and `[renderdN]` (`1 <= N < MAX_SLAVES`)
 * to index `N` in `configs_dest[]`. Resolves `num_threads = -1` to the online CPU core count
 * (`sysconf(_SC_NPROCESSORS_ONLN)`), validates Unix socket path length against
 * `sockaddr_un.sun_path`, and rejects duplicate or out-of-range section indices.
 *
 * @param ini              Pre-loaded iniparser dictionary, or `NULL` to load `config_file_name` locally.
 * @param config_file_name Path to the configuration file (used for loading if `ini == NULL` and in logs).
 * @param configs_dest     Destination array of `MAX_SLAVES` `renderd_config` entries.
 */
void process_renderd_sections(const dictionary *ini, const char *config_file_name, renderd_config *configs_dest)
{
	/* Flag set to 1 if this function loaded `ini` itself and must free it before returning */
	int ini_loaded_here = 0;
	/* Parsed slot index in `configs_dest[]` for the current `[renderd*]` section (-1 if none found) */
	int renderd_section_num = -1;
	/* Maximum byte buffer size of `sockaddr_un.sun_path` (socketname must be strictly shorter for NUL) */
	size_t renderd_socketname_maxlen = sizeof(((struct sockaddr_un *)0)->sun_path);

	if (!ini) {
		ini = iniparser_load(config_file_name);
		ini_loaded_here = 1;

		if (!ini) {
			g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file (process_renderd_sections): '%s'", config_file_name);
			exit(1);
		}
	}

	bzero(configs_dest, sizeof(renderd_config) * MAX_SLAVES);

	g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section(s)");

	for (int section_num = 0; section_num < iniparser_getnsec(INIPARSER_DICT_CAST(ini)); section_num++) {
		/* Current INI section name and prefix length of "renderd" */
		const char *section = iniparser_getsecname(INIPARSER_DICT_CAST(ini), section_num);
		int renderd_strlen = 7;

		if (strncmp(section, "renderd", renderd_strlen) == 0) { /* This is a renderd config section */
			if (strcmp(section, "renderd") == 0 || strcmp(section, "renderd0") == 0) {
				renderd_section_num = 0;
			} else {
				/* Pointer set by strtol() to verify a valid numeric suffix after "renderd" */
				char *endptr;
				renderd_section_num = strtol(&section[renderd_strlen], &endptr, 10);

				if (endptr == &section[renderd_strlen]) {
					g_logger(G_LOG_LEVEL_CRITICAL, "Invalid renderd section name: %s", section);
					exit(7);
				}
			}

			g_logger(G_LOG_LEVEL_DEBUG, "Parsing renderd config section %i: %s", renderd_section_num, section);

			if (renderd_section_num >= MAX_SLAVES) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Can't handle more than %i renderd config sections", MAX_SLAVES);
				exit(7);
			}

			if (configs_dest[renderd_section_num].name != NULL) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Duplicate renderd config section names for section %i: %s & %s", renderd_section_num, configs_dest[renderd_section_num].name, section);
				exit(7);
			}

			copy_string(section, &configs_dest[renderd_section_num].name, renderd_strlen + 2);

			process_config_int(ini, section, "ipport", &configs_dest[renderd_section_num].ipport, 0);
			process_config_int(ini, section, "dirty_queue_limit", &configs_dest[renderd_section_num].dirty_queue_limit, DIRTY_LIMIT);
			process_config_int(ini, section, "num_threads", &configs_dest[renderd_section_num].num_threads, NUM_THREADS);
			process_config_int(ini, section, "request_queue_limit", &configs_dest[renderd_section_num].request_queue_limit, REQ_LIMIT);
			process_config_string(ini, section, "iphostname", &configs_dest[renderd_section_num].iphostname, "", INILINE_MAX);
			process_config_string(ini, section, "pid_file", &configs_dest[renderd_section_num].pid_filename, RENDERD_PIDFILE, PATH_MAX);
			process_config_string(ini, section, "socketname", &configs_dest[renderd_section_num].socketname, RENDERD_SOCKET, PATH_MAX);
			process_config_string(ini, section, "stats_file", &configs_dest[renderd_section_num].stats_filename, "", PATH_MAX);
			process_config_string(ini, section, "tile_dir", &configs_dest[renderd_section_num].tile_dir, RENDERD_TILE_DIR, PATH_MAX);

			if (configs_dest[renderd_section_num].num_threads == -1) {
				/* Auto-detect worker thread count from online logical CPU count */
				configs_dest[renderd_section_num].num_threads = sysconf(_SC_NPROCESSORS_ONLN);
			}

			if (configs_dest[renderd_section_num].request_queue_limit < 1) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified request_queue_limit (%i) is too small, must be greater than or equal to %i.", configs_dest[renderd_section_num].request_queue_limit, 1);
				exit(7);
			}

			if (configs_dest[renderd_section_num].dirty_queue_limit < 0) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified dirty_queue_limit (%i) is too small, must be greater than or equal to %i.", configs_dest[renderd_section_num].dirty_queue_limit, 0);
				exit(7);
			}

			if (strnlen(configs_dest[renderd_section_num].socketname, PATH_MAX) >= renderd_socketname_maxlen) {
				g_logger(G_LOG_LEVEL_CRITICAL, "Specified socketname (%s) exceeds maximum allowed length of %zu.", configs_dest[renderd_section_num].socketname, renderd_socketname_maxlen);
				exit(7);
			}
		}
	}

	if (ini_loaded_here == 1) {
		iniparser_freedict((dictionary *)ini);
	}

	if (renderd_section_num < 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "No renderd config sections were found in file: %s", config_file_name);
		exit(1);
	}
}

/**
 * @brief Load and parse a complete `renderd.conf` file and populate global configuration state.
 *
 * Orchestrates parsing of `[renderd*]` sections into `config_slaves[]`, selects
 * `config = &config_slaves[active_renderd_section_num]`, parses `[mapnik]` into the
 * active config, parses all map sections into `maps[]`, totals `num_slave_threads` across
 * non-active instances, and logs the effective runtime configuration.
 *
 * @param config_file_name           Filesystem path to `renderd.conf`.
 * @param active_renderd_section_num Index (`0 .. MAX_SLAVES - 1`) of the `[renderd*]` section
 *                                   this process instance should run as.
 * @param log_level                  GLib log level used to emit the parsed configuration summary.
 */
void process_config_file(const char *config_file_name, int active_renderd_section_num, int log_level)
{
	num_slave_threads = 0;

	if (active_renderd_section_num < 0 || active_renderd_section_num >= MAX_SLAVES) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Active renderd section (%i) must be between %i and %i.", active_renderd_section_num, 0, MAX_SLAVES - 1);
		exit(1);
	}

	g_logger(log_level, "Parsing renderd config file '%s':", config_file_name);

	/* Parsed iniparser dictionary shared across all section-parsing helpers */
	const dictionary *ini = iniparser_load(config_file_name);

	if (!ini) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Failed to load config file (process_config_file): '%s'", config_file_name);
		exit(1);
	}

	free_renderd_sections(config_slaves);
	process_renderd_sections(ini, config_file_name, config_slaves);

	if (config_slaves[active_renderd_section_num].num_threads == 0) {
		g_logger(G_LOG_LEVEL_CRITICAL, "Active renderd section (%i) does not exist.", active_renderd_section_num);
		exit(1);
	}

	process_mapnik_section(ini, config_file_name, &config_slaves[active_renderd_section_num]);
	free_map_sections(maps);
	process_map_sections(ini, config_file_name, maps, config_slaves[active_renderd_section_num].tile_dir, config_slaves[active_renderd_section_num].num_threads);
	iniparser_freedict((dictionary *)ini);

	config = &config_slaves[active_renderd_section_num];

	/* Log debug details for all configured renderd sections and sum slave thread counts */
	for (int i = 0; i < MAX_SLAVES; i++) {
		if (config_slaves[i].num_threads == 0) {
			continue;
		}

		if (i == active_renderd_section_num) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): Active", i);
		} else {
			num_slave_threads += config_slaves[i].num_threads;
		}

		if (config_slaves[i].ipport > 0) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): ip socket = '%s:%i'", i, config_slaves[i].iphostname, config_slaves[i].ipport);
		} else {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): unix socketname = '%s'", i, config_slaves[i].socketname);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): num_threads = '%i'", i, config_slaves[i].num_threads);
		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): request_queue_limit = '%i'", i, config_slaves[i].request_queue_limit);
		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): dirty_queue_limit = '%i'", i, config_slaves[i].dirty_queue_limit);
		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): pid_file = '%s'", i, config_slaves[i].pid_filename);

		if (strnlen(config_slaves[i].stats_filename, PATH_MAX)) {
			g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): stats_file = '%s'", i, config_slaves[i].stats_filename);
		}

		g_logger(G_LOG_LEVEL_DEBUG, "\trenderd(%i): tile_dir = '%s'", i, config_slaves[i].tile_dir);
	}

	/* Log summary of the active renderd instance, Mapnik paths, and configured map styles */
	if (config->ipport > 0) {
		g_logger(log_level, "\trenderd: ip socket = '%s:%i'", config->iphostname, config->ipport);
	} else {
		g_logger(log_level, "\trenderd: unix socketname = '%s'", config->socketname);
	}

	g_logger(log_level, "\trenderd: num_threads = '%i'", config->num_threads);
	g_logger(log_level, "\trenderd: request_queue_limit = '%i'", config->request_queue_limit);
	g_logger(log_level, "\trenderd: dirty_queue_limit = '%i'", config->dirty_queue_limit);

	if (active_renderd_section_num == 0 && num_slave_threads > 0) {
		g_logger(log_level, "\trenderd: num_slave_threads = '%i'", num_slave_threads);
	}

	g_logger(log_level, "\trenderd: pid_file = '%s'", config->pid_filename);

	if (strnlen(config->stats_filename, PATH_MAX)) {
		g_logger(log_level, "\trenderd: stats_file = '%s'", config->stats_filename);
	}

	g_logger(log_level, "\trenderd: tile_dir = '%s'", config->tile_dir);
	g_logger(log_level, "\tmapnik:  font_dir = '%s'", config->mapnik_font_dir);
	g_logger(log_level, "\tmapnik:  font_dir_recurse = '%s'", config->mapnik_font_dir_recurse ? "true" : "false");
	g_logger(log_level, "\tmapnik:  plugins_dir = '%s'", config->mapnik_plugins_dir);

	for (int i = 0; i < XMLCONFIGS_MAX; i++) {
		if (maps[i].xmlname != NULL) {
			g_logger(log_level, "\tmap %i:   name(%s) file(%s) uri(%s) output_format(%s) htcp(%s) host(%s)", i, maps[i].xmlname, maps[i].xmlfile, maps[i].xmluri, maps[i].output_format, maps[i].htcpip, maps[i].host);
		}
	}
}
