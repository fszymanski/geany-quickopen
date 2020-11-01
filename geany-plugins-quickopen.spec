%define build_date %(date +"%Y%m%d")

%global debug_package %{nil}

%global commit 4d08da3623dc8004b8b0d1068caf04fd28515cb8
%global shortcommit %(c=%{commit}; echo ${c:0:7})

Name:           geany-plugins-quickopen
Version:        0
Release:        0.2.%{build_date}git%{shortcommit}%{?dist}
Summary:        Quickly open a file

License:        GPLv3+
URL:            https://github.com/fszymanski/geany-quickopen
Source0:        https://github.com/fszymanski/geany-quickopen/archive/%{commit}/geany-quickopen-%{shortcommit}.tar.gz

BuildRequires:  gcc
BuildRequires:  geany-devel

%description
Quick Open is a plugin for Geany that allows you to quickly open a file.

%prep
%autosetup -n geany-quickopen-%{commit}

%build
%make_build

%install
%make_install

%files
%license LICENSE
%{_libdir}/geany/geany-quickopen.so

%changelog
