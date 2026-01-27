#include "config.h"
#include "g_logger.h"
#include "renderd_config.h"

#include <glib.h>

int main(int argc, char **argv)
{
	char *config_file_name = (char *)RENDERD_CONFIG;
	char *process_function = (char *)"process_config_file";
	int active_renderd_section_num = 0;

	foreground = 1;

	if (argc > 1) {
		config_file_name = argv[1];
	}

	if (argc > 2) {
		active_renderd_section_num = atoi(argv[2]);
	}

	if (argc > 3) {
		process_function = argv[3];
	}

	if (strcmp(process_function, "process_config_file") == 0) {
		process_config_file(config_file_name, active_renderd_section_num, G_LOG_LEVEL_WARNING);
	}

	if (strcmp(process_function, "process_renderd_sections") == 0) {
		process_renderd_sections(NULL, config_file_name, config_slaves);
	}

	if (strcmp(process_function, "process_mapnik_section") == 0) {
		process_mapnik_section(NULL, config_file_name, &config_slaves[active_renderd_section_num]);
	}

	if (strcmp(process_function, "process_map_sections") == 0) {
		process_map_sections(NULL, config_file_name, maps, "", 0);
	}

	free_map_sections(maps);
	free_renderd_sections(config_slaves);

	exit(0);
}
