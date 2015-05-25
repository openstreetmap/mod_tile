#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/params.hpp>
#include <mapnik/datasource.hpp>
#include <mapnik/datasource_cache.hpp>

#include <syslog.h>

#include <boost/optional.hpp>

#include "parameterize_style.hpp"


static void parameterize_map_language(mapnik::Map &m, char * parameter) { 
    unsigned int i;
    char * data = strdup(parameter); 
    char * tok; 
    char name_replace[256]; 
    
    name_replace[0] = 0; 
    syslog(LOG_DEBUG, "Internationalizing map to language parameter: %s", parameter); 
    tok = strtok(data,","); 
    if (!tok) return; //No parameterization given 
    strncat(name_replace, ", coalesce(", 255); 
    while (tok) { 
        if (strcmp(tok,"_") == 0) { 
            strncat(name_replace,"name,", 255); 
        } else { 
            strncat(name_replace,"tags->'name:", 255); 
            strncat(name_replace, tok, 255); 
            strncat(name_replace,"',", 255); 
        } 
        tok = strtok(NULL, ","); 
        
    }
    free(data);
    name_replace[strlen(name_replace) - 1] = 0; 
    strncat(name_replace,") as name", 255); 
    for (i = 0; i < m.layer_count(); i++) { 
#if MAPNIK_VERSION >= 300000
        mapnik::layer& l = m.get_layer(i);
#else
        mapnik::layer& l = m.getLayer(i);
#endif
        mapnik::parameters params = l.datasource()->params(); 
        if (params.find("table") != params.end()) {
            boost::optional<std::string> table = params.get<std::string>("table");
            if (table && table->find(",name") != std::string::npos) {
                std::string str = *table;
                size_t pos = str.find(",name"); 
                str.replace(pos,5,name_replace); 
                params["table"] = str; 
#if MAPNIK_VERSION >= 200200
                l.set_datasource(mapnik::datasource_cache::instance().create(params));
#else
                l.set_datasource(mapnik::datasource_cache::instance()->create(params));
#endif
            } 
        } 
        
    } 
}
  

parameterize_function_ptr init_parameterization_function(char * function_name) {
    syslog(LOG_INFO, "Loading parameterization function for %s", function_name);
    if (strcmp(function_name, "") == 0) {
        return NULL;
    } else if (strcmp(function_name, "language") == 0) {
        return parameterize_map_language;
    } else {
        syslog(LOG_INFO, "WARNING: unknown parameterization function for %s", function_name);
    }
    return NULL;
}
