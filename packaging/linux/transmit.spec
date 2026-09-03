Name:           transmit
Version:        0.1.0
Release:        1%{?dist}
Summary:        Move a computer's environment to one running another operating system

License:        AGPL-3.0-or-later
URL:            https://github.com/neramc/transmit
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  libzstd-devel
BuildRequires:  xz-devel
BuildRequires:  openssl-devel
BuildRequires:  sqlite-devel
BuildRequires:  libsecret-devel

Requires:       qt6-qtdeclarative
Recommends:     NetworkManager
Suggests:       libsecret

%description
Transmit captures your files, the data and settings your programs keep, your
desktop preferences and the list of what you have installed; compresses all of
it onto removable media; and restores it on a machine running a different
operating system.

Locations are stored as meanings rather than paths, so a folder lands where it
belongs on the far side. Application state is moved to where that program looks
for it on the new system, and the paths recorded inside its settings are
corrected to match. Anything that cannot cross is reported rather than silently
dropped.

%prep
%autosetup

%build
%cmake -GNinja -DTRANSMIT_BUILD_TESTS=ON -DTRANSMIT_WITH_UPDATER=OFF
%cmake_build

%check
%ctest

%install
%cmake_install

%files
%license LICENSE.md
%doc README.md
%{_bindir}/transmit
%{_bindir}/transmit-cli
%{_datadir}/applications/transmit.desktop
%{_datadir}/icons/hicolor/*/apps/transmit.*

%changelog
* Fri Aug 22 2026 Transmit contributors <noreply@example.invalid> - 0.1.0-1
- First packaged release.
