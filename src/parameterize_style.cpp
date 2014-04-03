#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>

#include <syslog.h>
#include <boost/variant.hpp>

#include "parameterize_style.hpp"


static void parameterize_map_language(mapnik::Map &m, char * parameter) { 
    int i; 
    char * data = strdup(parameter); 
    char * tok; 
    char ** ctx; 
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
        mapnik::layer& l = m.getLayer(i); 
        
        mapnik::parameters params = l.datasource()->params(); 
        if (params.find("table") != params.end()) { 
            if (boost::get<std::string>(params["table"]).find(",name") != std::string::npos) { 
                std::string str = boost::get<std::string>(params["table"]); 
                size_t pos = str.find(",name"); 
                str.replace(pos,5,name_replace); 
                params["table"] = str; 
#if MAPNIK_VERSION >= 200200
                boost::shared_ptr<mapnik::datasource> ds = mapnik::datasource_cache::instance().create(params); 
#else
                boost::shared_ptr<mapnik::datasource> ds = mapnik::datasource_cache::instance()->create(params);
#endif
                l.set_datasource(ds); 
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
