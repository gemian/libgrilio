Name: libgrilio
Version: 1.0.36
Release: 0
Summary: RIL I/O library
Group: Development/Libraries
License: BSD
URL: https://git.sailfishos.org/mer-core/libgrilio
Source: %{name}-%{version}.tar.bz2
Requires: libglibutil >= 1.0.10
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libglibutil) >= 1.0.10
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Provides glib-based RIL I/O library

%package devel
Summary: Development library for %{name}
Requires: %{name} = %{version}
Requires: pkgconfig

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
make KEEP_SYMBOLS=1 release pkgconfig

%install
rm -rf %{buildroot}
make install-dev DESTDIR=%{buildroot}

%check
make -C test test

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/%{name}.so.*

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/*.pc
%{_libdir}/%{name}.so
%{_includedir}/grilio/*.h
