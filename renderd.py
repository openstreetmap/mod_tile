#!/usr/bin/python
#
# mod_tile rendering daemon example written in Python.
# The code is mostly a direct port of the C implementation.
#
# This is currently experimental and not intended as a replacement
# of the C implementation, but works surpringly well. It should be
# easier to produce custom variations of the rendering pipeline,
# e.g. such as compositing tiles from multiple layers.
#
# It needs more work to make it more Pythonic, split it
# into more appropriate classes, add documentation, fix bugs etc.
#
# I'm not yet convinced this is the best approach to integrating
# Python with the render daemon. Two other options I'm considering:
#
# - Use the C renderd code with python binding allowing the replacement
# of just the core tile rendering code (this is the bit that people
# may want to tweak)
#
# - Split the functionality into a seperate queue handler daemon and
# render daemon. This would remove a lot of the complexity around the
# request handling which most people probably won't want to touch.
# The queue handler might stay in C with a smaller python rendering daemon

import sys, os
import SocketServer
import struct
import thread
import threading
import socket
import ConfigParser
import mapnik
import time
import errno
from math import pi,cos,sin,log,exp,atan
from StringIO import StringIO

import cairo
import cStringIO

MAX_ZOOM = 18
METATILE = 8
META_MAGIC = "META"

class protocol:
    # ENUM values for commandStatus field in protocol packet
    Ignore = 0
    Render = 1
    Dirty = 2
    Done = 3
    NotDone = 4

class ProtocolPacket:
    def __init__(self, version, fields = ""):
        self.version = version
        self.xmlname = ""
        self.x = 0
        self.y = 0
        self.z = 0
        self.mx = 0
        self.my = 0
        self.commandStatus = protocol.Ignore
        self.fields = fields

    def len(self):
        return struct.calcsize(self.fields)

    def bad_request(self):
        # Check that the requested (x,y,z) is invalid
        x = self.x
        y = self.y
        z = self.z

        if (z < 0) or (z > MAX_ZOOM):
            return True
        limit = (1 << z) -1
        if (x < 0) or (x > limit):
            return True
        if (y < 0) or (y > limit):
            return True
        return False

    def meta_tuple(self):
        # This metatile tuple is used to identify duplicate request in the rendering queue
        return (self.xmlname, self.mx, self.my, self.z)

class ProtocolPacketV1(ProtocolPacket):
    def __init__(self):
        ProtocolPacket(1)
        self.fields = "5i"

    def receive(self, data, dest):
        version, request, x, y, z = struct.unpack(self.fields, data)

        if version != 1:
            print "Received V1 packet with incorect version %d" % version
        else:
            #print "Got V1 request, command(%d), x(%d), y(%d), z(%d)" \
            #    % (request, x, y, z)
            self.commandStatus = request
            self.x = x
            self.y = y
            self.z = z
            self.xmlname = "default"
            # Calculate Meta-tile value for this x/y
            self.mx = x & ~(METATILE-1)
            self.my = y & ~(METATILE-1)
            self.dest = dest


    def send(self, status):
        x = self.x
        y = self.y
        z = self.z
        data = struct.pack(self.fields, (1, status, x, y, z))
        try: 
            self.dest.send(data)
        except socket.error, e:
               if e[0] != errno.EBADF:
                   raise


class ProtocolPacketV2(ProtocolPacket):
    def __init__(self):
        ProtocolPacket(2)
        self.fields = "5i41sxxx"

    def receive(self, data, dest):
        version, request, x, y, z, xmlname = struct.unpack(self.fields, data)

        if version != 2:
            print "Received V2 packet with incorect version %d" % version
        else:
            #print "Got V2 request, command(%d), xmlname(%s), x(%d), y(%d), z(%d)" \
            #    % (request, xmlname, x, y, z)
            self.commandStatus = request
            self.x = x
            self.y = y
            self.z = z
            self.xmlname = xmlname.rstrip('\000') # Remove trailing NULs
            # Calculate Meta-tile value for this x/y
            self.mx = x & ~(METATILE-1)
            self.my = y & ~(METATILE-1)
            self.dest = dest

    def send(self, status):
        x = self.x
        y = self.y
        z = self.z
        xmlname = self.xmlname
        data = struct.pack(self.fields, 2, status, x, y, z, xmlname)
        try:
            self.dest.send(data)
        except socket.error, e:
               if e[0] != errno.EBADF:
                   raise

DEG_TO_RAD = pi/180
RAD_TO_DEG = 180/pi


class SphericalProjection:
    def __init__(self,levels=18):
        self.Bc = []
        self.Cc = []
        self.zc = []
        self.Ac = []
        c = 256
        for d in range(0,levels+1):
            e = c/2;
            self.Bc.append(c/360.0)
            self.Cc.append(c/(2 * pi))
            self.zc.append((e,e))
            self.Ac.append(c)
            c *= 2

    def minmax(self, a,b,c):
        a = max(a,b)
        a = min(a,c)
        return a

    def fromLLtoPixel(self,ll,zoom):
         d = self.zc[zoom]
         e = round(d[0] + ll[0] * self.Bc[zoom])
         f = self.minmax(sin(DEG_TO_RAD * ll[1]),-0.9999,0.9999)
         g = round(d[1] + 0.5*log((1+f)/(1-f))*-self.Cc[zoom])
         return (e,g)

    def fromPixelToLL(self,px,zoom):
         e = self.zc[zoom]
         f = (px[0] - e[0])/self.Bc[zoom]
         g = (px[1] - e[1])/-self.Cc[zoom]
         h = RAD_TO_DEG * ( 2 * atan(exp(g)) - 0.5 * pi)
         return (f,h)


class RenderThread:
    def __init__(self, tile_path, styles, queue_handler):
        self.tile_path = tile_path
        self.queue_handler = queue_handler
        self.maps = {}
        self.prj = {}
        for xmlname in styles:
            #print "Creating Mapnik map object for %s with %s" % (xmlname, styles[xmlname])
            m = mapnik.Map(256, 256)
            self.maps[xmlname] = m
            # Load XML style
            mapnik.load_map(m, styles[xmlname], True)
            # Obtain <Map> projection
            self.prj[xmlname] = mapnik.Projection(m.srs)

        # Projects between tile pixel co-ordinates and LatLong (EPSG:4326)
        self.tileproj = SphericalProjection(MAX_ZOOM)

    def render_with_agg(self, m, size):
        # Render image with default Agg renderer
        im = mapnik.Image(size, size)
        mapnik.render(m, im)
        return im

    def render_with_cairo(self, m, size):
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
        mapnik.render(m, surface)
        return mapnik.Image.from_cairo(surface)

    def split_meta_image(self, im, sz, format = 'png256'):
        # Split image up into NxN grid of tile images
        tiles = {}
        for yy in range(0,sz):
            for xx in range(0,sz):
                view = im.view(xx * 256 , yy * 256, 256, 256)
                tile = view.tostring(format)
                tiles[(xx, yy)] = tile

        return tiles


    def render_meta(self, m, style, x, y, z, sz):
        # Calculate pixel positions of bottom-left & top-right
        p0 = (x * 256, (y + sz) * 256)
        p1 = ((x + sz) * 256, y * 256)

        # Convert to LatLong (EPSG:4326)
        l0 = self.tileproj.fromPixelToLL(p0, z);
        l1 = self.tileproj.fromPixelToLL(p1, z);

        # Convert to map projection (e.g. mercator co-ords EPSG:900913)
        c0 = self.prj[style].forward(mapnik.Coord(l0[0],l0[1]))
        c1 = self.prj[style].forward(mapnik.Coord(l1[0],l1[1]))

        # Bounding box for the meta-tile
        bbox = mapnik.Envelope(c0.x,c0.y, c1.x,c1.y)
        render_size = 256 * sz
        m.resize(render_size, render_size)
        m.zoom_to_box(bbox)
        m.buffer_size = 128

        im = self.render_with_agg(m, render_size)
        #im = self.render_with_cairo(m, render_size)
        return self.split_meta_image(im, sz)

    def render_request(self, t):
        (xmlname, x, y, z) = t
        # Calculate the meta tile size to use for this zoom level
        size = min(METATILE, 1 << z)
        try:
            m = self.maps[xmlname]
        except KeyError:
            print "No map for: '%s'" % xmlname
            return False
        tiles = self.render_meta(m, xmlname, x, y, z, size)
        self.meta_save(xmlname, x, y, z, size, tiles)

        print "Done xmlname(%s) z(%d) x(%d-%d) y(%d-%d)" % \
            (xmlname, z, x, x+size-1, y, y+size-1)

        return True;

    def xyz_to_meta(self, xmlname, x,y, z):
        mask = METATILE -1
        x &= ~mask
        y &= ~mask
        hashes = {}

        for i in range(0,5):
            hashes[i] = ((x & 0x0f) << 4) | (y & 0x0f)
            x >>= 4
            y >>= 4

        meta = "%s/%s/%d/%u/%u/%u/%u/%u.meta" % (self.tile_path, xmlname, z, hashes[4], hashes[3], hashes[2], hashes[1], hashes[0])
        return meta

    def xyz_to_meta_offset(self, xmlname, x,y, z):
        mask = METATILE -1
        offset = (x & mask) * METATILE + (y & mask)
        return offset


    def meta_save(self, xmlname, x, y, z, size, tiles):
        #print "Saving %d tiles" % (size * size)
        meta_path = self.xyz_to_meta(xmlname, x, y, z)
        d = os.path.dirname(meta_path)
        if not os.path.exists(d):
            try:
                os.makedirs(d)
            except OSError:
                # Multiple threads can race when creating directories,
                # ignore exception if the directory now exists
                if not os.path.exists(d):
                    raise

        tmp = "%s.tmp.%d" % (meta_path, thread.get_ident())
        f = open(tmp, "w")

        f.write(struct.pack("4s4i", META_MAGIC, METATILE * METATILE, x, y, z))
        offset = len(META_MAGIC) + 4 * 4
        # Need to pre-compensate the offsets for the size of the offset/size table we are about to write
        offset += (2 * 4) * (METATILE * METATILE)
        # Collect all the tile sizes
        sizes = {}
        offsets = {}
        for xx in range(0, size):
            for yy in range(0, size):
                mt = self.xyz_to_meta_offset(xmlname, x+xx, y+yy, z)
                sizes[mt] = len(tiles[(xx, yy)])
                offsets[mt] = offset
                offset += sizes[mt]
        # Write out the offset/size table
        for mt in range(0, METATILE * METATILE):
            if mt in sizes:
                f.write(struct.pack("2i", offsets[mt], sizes[mt]))
            else:
                f.write(struct.pack("2i", 0, 0))
        # Write out the tiles
        for xx in range(0, size):
            for yy in range(0, size):
                f.write(tiles[(xx, yy)])

        f.close()
        os.rename(tmp, meta_path)
        #print "Wrote: %s" % meta_path

    def loop(self):
        while True:
            #Fetch a meta-tile to render
            r = self.queue_handler.fetch()
            rendered = self.render_request(r)
            # Retrieve all requests for this meta-tile
            requests = self.queue_handler.pop_requests(r)
            for request in requests:
                if (request.commandStatus == protocol.Render):
                    if rendered == True:
                        request.send(protocol.Done)
                    else:
                        request.send(protocol.NotDone)


def start_renderers(num_threads, tile_path, styles, queue_handler):
    for i in range(num_threads):
        renderer = RenderThread(tile_path, styles, queue_handler)
        render_thread = threading.Thread(target=renderer.loop)
        render_thread.setDaemon(True)
        render_thread.start()
        print "Started render thread %s" % render_thread.getName()

class RequestQueues:
    def __init__(self, request_limit = 32, dirty_limit = 1000):
        # We store requests in several lists
        # - Incoming render requests are initally put into the request queue
        # If the request queue is full then the new request is demoted to the dirty queue
        # - Incoming 'dirty' requests are put into the dirty queue, or dropped if this is full
        # - The render queue holds the requests which are in progress by the render threads
        self.requests = {}
        self.dirties = {}
        self.rendering = {}

        self.request_limit = request_limit
        self.dirty_limit = dirty_limit
        self.not_empty = threading.Condition()


    def add(self, request):
        self.not_empty.acquire()
        try:
            # Before adding this new request we first look if this tile is already pending
            # If so, the new request is tacked on to the existing one
            # FIXME: Add short-circuit for overload condition?
            t = request.meta_tuple()
            if t in self.rendering:
                self.rendering[t].append(request)
                return "rendering"
            if t in self.requests:
                self.requests[t].append(request)
                return "requested"
            if t in self.dirties:
                self.dirties[t].append(request)
                return "dirty"
            # If we've reached here then there are no existing requests for this tile
            if (request.commandStatus == protocol.Render) and (len(self.requests) < self.request_limit):
                self.requests[t] = [request]
                self.not_empty.notify()
                return "requested"
            if len(self.dirties) < self.dirty_limit:
                self.dirties[t] = [request]
                self.not_empty.notify()
                return "dirty"
            return "dropped"
        finally:
            self.not_empty.release()


    def fetch(self):
        # Fetches a request tuple from the request or dirty queue
        # The requests are moved to the rendering queue while they are being rendered
        self.not_empty.acquire()
        try:
            while (len(self.requests) == 0) and (len(self.dirties) == 0):
                self.not_empty.wait()
            # Pull request from one of the incoming queues
            try:
                item = self.requests.popitem()
            except KeyError:
                try:
                    item = self.dirties.popitem()
                except KeyError:
                    print "Odd, queues empty"
                    return

            t = item[0]
            self.rendering[t] = item[1]
            return t
        finally:
            self.not_empty.release()

    def pop_requests(self, t):
        # Removes this tuple from the rendering queue
        # and returns the list of request for the tuple
        self.not_empty.acquire()
        try:
            return self.rendering.pop(t)
        except KeyError:
            # Should never happen. It implies the requests queues are broken
            print "WARNING: Failed to locate request in rendering list!"
        finally:
            self.not_empty.release()


class ThreadedUnixStreamHandler(SocketServer.BaseRequestHandler):

    def rx_request(self, request):
        if (request.commandStatus != protocol.Render) \
           and (request.commandStatus != protocol.Dirty):
               return

        if request.bad_request():
            if (request.commandStatus == protocol.Render):
                request.send(protocol.NotDone)
            return

        #cur_thread = threading.currentThread()
        #print "%s: xml(%s) z(%d) x(%d) y(%d)" % \
        #    (cur_thread.getName(), request.xmlname, request.z, request.x, request.y)

        status = self.server.queue_handler.add(request)
        if status in ("rendering", "requested"):
            # Request queued, response will be sent on completion
            return

        # The tile won't be rendered soon, tell the requestor straight away
        if (request.commandStatus == protocol.Render):
            request.send(protocol.NotDone)

    def handle(self):
        cur_thread = threading.currentThread()
        #print "%s: New connection" % cur_thread.getName()
        len_v1 = ProtocolPacketV1().len()
        len_v2 = ProtocolPacketV2().len()
        max_len = max(len_v1, len_v2)

        while True:
            try:
                data = self.request.recv(max_len)
            except socket.error, e:
                if e[0] == errno.ECONNRESET:
                    #print "Connection reset by peer"
                    break
                else:
                    raise

            if len(data) == len_v1:
                req_v1 = ProtocolPacketV1()
                req_v1.receive(data, self.request)
                self.rx_request(req_v1)
            if len(data) == len_v2:
                req_v2 = ProtocolPacketV2()
                req_v2.receive(data, self.request)
                self.rx_request(req_v2)
            elif len(data) == 0:
                #print "%s: Connection closed" % cur_thread.getName()
                break
            else:
                print "Invalid request length %d" % len(data)
                break

class ThreadedUnixStreamServer(SocketServer.ThreadingMixIn, SocketServer.UnixStreamServer):
    def __init__(self, address, queue_handler, handler):
        if(os.path.exists(address)):
           os.unlink(address)
        self.address = address
        self.queue_handler = queue_handler
        SocketServer.UnixStreamServer.__init__(self, address, handler)
        self.daemon_threads = True

def listener(address, queue_handler):
    # Create the server
    server = ThreadedUnixStreamServer(address, queue_handler, ThreadedUnixStreamHandler)
    # The socket needs to be writeable by Apache
    os.chmod(address, 0666)
    # Loop forever servicing requests
    server.serve_forever()

def display_config(config):
    for xmlname in config.sections():
        if xmlname != "renderd" and xmlname != "mapnik":
            print "Layer name: %s" % xmlname
            uri = config.get(xmlname, "uri")
            xml = config.get(xmlname, "xml")
            print "    URI(%s) = XML(%s)" % (uri, xml)

def read_styles(config):
    styles = {}
    for xmlname in config.sections():
        if xmlname != "renderd" and xmlname != "mapnik":
            styles[xmlname] = config.get(xmlname, "xml")
    return styles

if __name__ == "__main__":
    try:
        cfg_file = os.environ['RENDERD_CFG']
    except KeyError:
        cfg_file = "/etc/renderd.conf"

    # Unifont has a better character coverage and is used as a fallback for DejaVu
    # if you use a style based on the osm-template-fonset.xml
    mapnik.FontEngine.instance().register_font("/home/jburgess/osm/fonts/unifont-5.1.20080706.ttf")

    default_cfg = StringIO("""
[renderd]
socketname=/tmp/osm-renderd
num_threads=4
tile_dir=/var/lib/mod_tile
""")

    config = ConfigParser.ConfigParser()
    config.readfp(default_cfg)
    config.read(cfg_file)
    display_config(config)
    styles = read_styles(config)

    num_threads    = config.getint("renderd", "num_threads")
    renderd_socket = config.get("renderd", "socketname")
    tile_dir       = config.get("renderd", "tile_dir")

    queue_handler = RequestQueues()
    start_renderers(num_threads, tile_dir, styles, queue_handler)
    listener(renderd_socket, queue_handler)
