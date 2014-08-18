#
# spec file for package apache2-mod_tile
#
# Copyright (c) 2013 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#

#%define debug_package %{nil}
%define METATILE 8
%define apxs /usr/sbin/apxs
BuildRequires:  httpd-devel

Name:           mod_tile
Version:	0.5
Release:	20140123%{?dist}
Requires:       httpd
Summary:        Apache module for map tile handling
License:        GPL-2.0+
Group:          Productivity/Networking/Web/Servers
Url:            http://wiki.openstreetmap.org/wiki/Mod_tile
Source:         mod_tile-%{version}.tar.bz2
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  gcc-c++
BuildRequires:  libtool
BuildRequires:  make
#BuildRequires:  pkgconfig
BuildRequires:  boost-devel
BuildRequires:  freetype-devel
BuildRequires:  libicu-devel >= 4.2
BuildRequires:  proj-devel
BuildRequires:  mapnik-devel >= 2.2.0-6
BuildRequires:  openssl-devel

%description
Mod tile is a system to serve raster tiles for example to use within a slippy map.
It provides a dynamic combination of efficient caching and on the fly rendering.
Due to its dynamic rendering, only a small fraction of overall tiles need to be
kept on disk, reducing the resources required. At the same time, its caching
strategy allows for a high performance serving and can support several thousand
requests per second.

Mod_tile was originally written for serving the tiles of the main OSM map (Mapnik
layer), but since is being used on a variety of different servers providing maps
ontop of OpenStreetMap data.

%package -n renderd
Summary:        Render daemon for Apache2 map tile module
Group:          Productivity/Networking/Web/Servers

%description -n renderd
Default rendering daemon for mod_tile.

%prep
%setup -q -n mod_tile
sed -i 's/#define METATILE (8)/#define METATILE (%{METATILE})/' includes/render_config.h

%build
export CPPFLAGS="-I/usr/include/agg2"
./autogen.sh
%configure \
    --with-apxs="%{apxs}"
%__make %{?_smp_flags} -j8
cd extra && make

%post -n renderd -p /sbin/ldconfig
%postun -n renderd -p /sbin/ldconfig

%install
%makeinstall
make DESTDIR=%{buildroot} install-mod_tile
cp -a extra/meta2tile %{buildroot}/%{_bindir}

%clean
%{?buildroot:%__rm -rf "%{buildroot}"}

%files -n renderd
%defattr(-,root,root)
%doc COPYING
#%{_bindir}/convert_meta
%{_bindir}/render*
%{_bindir}/meta2tile
%config %{_sysconfdir}/renderd.conf
%{_libdir}/libiniparser*
%exclude %{_libdir}/libiniparser.so
%exclude %{_libdir}/libiniparser.a
%{_mandir}/man1/render_expired.1.gz
%{_mandir}/man1/render_list.1.gz
%{_mandir}/man1/render_old.1.gz
%{_mandir}/man1/render_speedtest.1.gz
%{_mandir}/man8/renderd.8.gz

%files
%defattr(-,root,root)
%doc COPYING
%{_libdir}/httpd/modules/mod_tile.so

%changelog
* Thu Jan 23 2014 kay.diam@gmail.com
- Bump version
* Fri Dec 27 2013 kay.diam@gmail.com
- Bump version and added METATILE const
* Mon Sep 09 2013 kay.diam@gmail.com
- Adaptation for CentOS 6.4
* Wed Feb 20 2013 opensuse@dstoecker.de
- update to revision 29268
* Thu Jan 17 2013 BSipos@rkf-eng.com
- Added ICU package version requirement to match mapnik requirement.
* Thu Jul 26 2012 opensuse@dstoecker.de
- initial version
