Name:           mod_tile
Version:        0.8.0.beta
Release:        1%{?dist}
Summary:        Renders and serves OpenStreetMap map tiles using apache
URL:            https://github.com/openstreetmap/mod_tile
Source0:        %{name}-%{version}.tar.gz
Source1:        mod_tile.conf
Source2:        mod_tile.module.conf
Source3:        renderd.service
Source4:        renderd.tmpfiles
Source5:        netdata-mod_tile.chart.py
Source6:        netdata-mod_tile.conf
Source7:        netdata-renderd.chart.py
Source8:        netdata-renderd.conf
Patch0:         mod_tile-fix-renderd-conf.patch
License:        GPLv2+
Requires:       httpd
BuildRoot:      %{_tmppath}/%{name}-%{version}-root
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  binutils
BuildRequires:  boost-devel
BuildRequires:  cairo-devel
BuildRequires:  cmake
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  gdal-devel
BuildRequires:  glib2
BuildRequires:  pkgconfig
BuildRequires:  glib2-devel
BuildRequires:  harfbuzz-devel
BuildRequires:  httpd-devel
BuildRequires:  iniparser-devel
BuildRequires:  kernel-devel
BuildRequires:  libcurl-devel
BuildRequires:  libicu-devel
BuildRequires:  libjpeg-devel
BuildRequires:  libmemcached-devel
BuildRequires:  librados-devel
BuildRequires:  libxml2-devel
BuildRequires:  libtiff-devel
BuildRequires:  libtool
BuildRequires:  libwebp-devel
BuildRequires:  make
BuildRequires:  mapnik-devel
BuildRequires:  proj-devel
BuildRequires:  systemd
BuildRequires:  sqlite-devel
Requires:       renderd = %{version}

%description
A program to efficiently render and serve map tiles for
OpenStreetMap map using Apache and Mapnik.

%package -n renderd
Summary:        Renders OpenStreetMap map tiles daemon
Requires(post): systemd-sysv

%description -n renderd
Renders OpenStreetMap map tiles for mod_tile

%package munin
Summary:        Munin plugins for mod_tile
Requires:       munin-node

%description munin
Munin plugins for mod_tile

%package netdata
Summary:        netdata plugins for mod_tile
Requires:       netdata

%description netdata
netdata plugins for mod_tile

%prep
%setup -q -n mod_tile-0.8.0-beta  
%patch0
./autogen.sh

%build
%{configure}
%{__make}
%{__make} test

#%%cmake \
#       %{_cmake_skip_rpath} \
#       -DCMAKE_BUILD_TYPE:STRING=Release \
#       -DENABLE_TESTS:BOOL=ON
#%%cmake_build
#%%ctest

%install
%{__make} install DESTDIR="%{buildroot}"
%{__make} install-mod_tile DESTDIR="%{buildroot}"
%{__mkdir_p} %{buildroot}%{_sysconfdir}/httpd/conf.d
%{__mkdir_p} %{buildroot}%{_sysconfdir}/httpd/conf.modules.d
%{__install} -Dp -m0644 %{SOURCE1} %{buildroot}%{_sysconfdir}/httpd/conf.d/%{name}.conf
%{__install} -Dp -m0644 %{SOURCE2} %{buildroot}%{_sysconfdir}/httpd/conf.modules.d/99-%{name}.conf
%{__install} -Dp -m0644 %{SOURCE4} %{buildroot}%{_sysconfdir}/renderd.conf
%{__install} utils/{openstreetmap-tiles-update-expire,openstreetmap-tiles-update-rerender,osmosis-db_replag,render_all} %{buildroot}%{_bindir}/
%{__mkdir_p} %{buildroot}%{_datadir}/renderd/example-map
%{__install} utils/example-map/* %{buildroot}%{_datadir}/renderd/example-map/
%{__mkdir_p} %{buildroot}%%{_mandir}/{man1,man5}
%{__install} docs/man/*.1 %{buildroot}%{_mandir}/man1/
%{__install} docs/man/*.5 %{buildroot}%{_mandir}/man5/

# Daemons must be located on %%sbindir
%{__mkdir_p} %{buildroot}%{_sbindir}
%{__mv} %{buildroot}%{_bindir}/renderd %{buildroot}%{_sbindir}/renderd
%{__mkdir_p} %{buildroot}%{_localstatedir}/lib/mod_tile
%{__install} -Dp -m0644 %{SOURCE3} %{buildroot}%{_unitdir}/renderd.service

# munin plugins
%{__mkdir_p} %{buildroot}%{_datadir}/munin/plugins
%{__install} -Dp -m0755 utils/munin/* %{buildroot}%{_datadir}/munin/plugins

# netdata plugins with conf
%{__mkdir_p} %{buildroot}%{_libexecdir}/netdata/python.d/
%{__mkdir_p} %{buildroot}%{_sysconfdir}/netdata/python.d/
%{__install} -Dp -m0644 %{SOURCE5} %{buildroot}%{_libexecdir}/netdata/python.d/mod_tile.chart.py
%{__install} -Dp -m0644 %{SOURCE7} %{buildroot}%{_libexecdir}/netdata/python.d/renderd.chart.py
%{__install} -Dp -m0644 %{SOURCE6} %{buildroot}%{_sysconfdir}/netdata/python.d/mod_tile.conf
%{__install} -Dp -m0644 %{SOURCE8} %{buildroot}%{_sysconfdir}/netdata/python.d/renderd.conf

%pre -n renderd
getent group osm > /dev/null || groupadd -r osm
getent passwd osm > /dev/null || useradd -r -g osm -c "OpenStreetMap User" -s /sbin/nologin -d /var/lib/mod_tile osm

%post -n renderd
%if 0%{?systemd_post:1}
%systemd_post renderd.service
%else
if [ $1 = 1 ]; then
    /usr/bin/systemctl daemon-reload >/dev/null 2>&1 || :
    /usr/bin/systemd-tmpfiles --create >/dev/null 2>&1 || :
fi
%endif

%preun -n renderd
%if 0%{?systemd_preun:1}
%systemd_preun renderd.service
%else
if [ "$1" = 0 ] ; then
    # Package removal, not upgrade
    /usr/bin/systemctl --no-reload disable renderd.service >/dev/null 2>&1 || :
    /usr/bin/systemctl stop renderd.service >/dev/null 2>&1 || :
fi
exit 0
%endif

%postun -n renderd
%if 0%{?systemd_postun_with_restart:1}
%systemd_postun_with_restart renderd.service
%else
/usr/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ]; then
# Package upgrade, not uninstall
    /usr/bin/systemctl try-restart renderd.service >/dev/null 2>&1 || :
fi
%endif

%files
%doc README.rst docs
%{_libdir}/httpd/modules/mod_tile.so
%config(noreplace) %{_sysconfdir}/httpd/conf.d/%{name}.conf
%config %{_sysconfdir}/httpd/conf.modules.d/99-%{name}.conf

%files -n renderd
%config(noreplace) %{_sysconfdir}/renderd.conf
%{_sbindir}/renderd
%{_bindir}/render_expired
%{_bindir}/render_list
%{_bindir}/render_old
%{_bindir}/render_speedtest
%{_bindir}/{openstreetmap-tiles-update-expire,openstreetmap-tiles-update-rerender,osmosis-db_replag,render_all}
%{_mandir}/man1/*
%{_mandir}/man5/*
%attr(0755,osm,osm) %dir %{_localstatedir}/lib/mod_tile
%{_unitdir}/renderd.service
%{_datadir}/renderd/example-map/*

%files munin
%{_datadir}/munin/plugins/*

%files netdata
%{_libexecdir}/netdata/python.d/*.chart.py*
%config(noreplace) %{_sysconfdir}/netdata/python.d/*.conf

%changelog
* Fri Jan 10 2025 Zenon Panoussis <oracle@provocation.net> - 0.7.1
- up to 0.8.0.beta

* Sun May 5 2024 Zenon Panoussis <oracle@provocation.net> - 0.7.1
- up to 0.7.1

* Sun Mar 3 2024 Zenon Panoussis <oracle@provocation.net> - 0.7.0
- bumped to source release 0.7.0
- adjusted patch0
- removed slippymap.html, no longer in source
- removed meta2tile, has been dropped upstream
- added buildreqs gcc-c++, binutils, sqlite-devel, libcurl-devel,
  memcached-devel and librados-devel
- fixed manpages

* Tue Jul 04 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.8
- netdata subpackage

* Thu Jun 29 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.7
- munin subpackage
- Add extra/meta2tile

* Sun Jun 18 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.6
- Stop providing /var/lib/mod-tile directory
- Fix osm home permissions

* Fri Jun 16 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.5
- Remove group
- Rebuild against mapnik 3

* Thu Jun 15 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.4
- Run systemd-tmpfiles --create on post
- Fix osm home to /var/lib/mod_tile

* Mon Jun 12 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.3
- Fix renderd conf and 

* Mon Jun 12 2017 Didier Fabert <dfabert@b2pweb.com> 0.5-0.2
- First package
