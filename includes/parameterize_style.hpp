/*
 * Copyright (c) 2007 - 2020 by mod_tile contributors (see AUTHORS file)
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

#ifndef PARAMETERIZE_HPP
#define PARAMETERIZE_HPP

typedef void (*parameterize_function_ptr)(mapnik::Map &m, char * parameter);

parameterize_function_ptr init_parameterization_function(char * function_name);

#endif
