#
# this is used/needed by the APACHE2 build system
#

MOD_TILE = mod_tile dir_utils store

mod_tile.la: ${MOD_TILE:=.slo}
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version ${MOD_TILE:=.lo}

DISTCLEAN_TARGETS = modules.mk

shared =  mod_tile.la

