#!/usr/bin/ruby
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
