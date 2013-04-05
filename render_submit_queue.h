/*
Copyright © 2013 mod_tile contributors

This file is part of mod_tile.

mod_tile is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

mod_tile is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RENDERSUBQUEUE_H
#define RENDERSUBQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif


int work_complete;

void enqueue(const char *xmlname, int x, int y, int z);
void spawn_workers(int num, const char *socketpath, int maxLoad);
void wait_for_empty_queue();
void finish_workers();

#ifdef __cplusplus
}
#endif

#endif
