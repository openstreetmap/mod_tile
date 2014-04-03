#ifndef PARAMETERIZE_HPP
#define PARAMETERIZE_HPP

typedef void (*parameterize_function_ptr)(mapnik::Map &m, char * parameter);

parameterize_function_ptr init_parameterization_function(char * function_name);

#endif
