#!/usr/bin/ruby
# -*- coding: utf-8 -*-

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

require 'mysql'
require 'date'
require 'time'
require 'fileutils'

dbh = nil
dbh = Mysql.real_connect('localhost', 'tile', 'tile', 'tile')
dbh.query_with_result = false
dbh.query("select x,y,z,data,created_at from tiles" )
res = dbh.use_result

while row = res.fetch_hash do
    x = row['x']
    y = row['y']
    z = row['z']
    created_at = Time.parse(row['created_at'])

    path = "/var/www/html/osm_tiles2/#{z}/#{x}"
    FileUtils.mkdir_p(path)

    print "x(#{x}) y(#{y}) z(#{z}), created_at(#{created_at.to_i})\n"

    f = File.new("#{path}/#{y}.png", "w")
    f.print row['data']
    f.close
    File.utime(created_at,created_at,"#{path}/#{y}.png")
end
puts "Number of rows returned: #{res.num_rows}"

res.free
