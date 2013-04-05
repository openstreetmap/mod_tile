# Copyright © 2013 mod_tile contributors
#
# This file is part of mod_tile.
#
# mod_tile is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# mod_tile is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with mod_tile.  If not, see <http://www.gnu.org/licenses/>.

#
# this is used/needed by the APACHE2 build system
#

MOD_TILE = mod_tile dir_utils store

mod_tile.la: ${MOD_TILE:=.slo}
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version ${MOD_TILE:=.lo}

DISTCLEAN_TARGETS = modules.mk

shared =  mod_tile.la

