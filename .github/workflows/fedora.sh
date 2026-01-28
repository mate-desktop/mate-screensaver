#!/usr/bin/bash

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Fedora
requires=(
	ccache # Use ccache to speed up build
)

requires+=(
	autoconf-archive
	dbus-glib-devel
	desktop-file-utils
	gcc
	git
	gtk3-devel
	libX11-devel
	libXScrnSaver-devel
	libXinerama-devel
	libXmu-devel
	libXtst-devel
	libXxf86vm-devel
	libmatekbd-devel
	libnotify-devel
	make
	mate-common
	mate-desktop-devel
	mate-menus-devel
	mesa-libGL-devel
	pam-devel
	redhat-rpm-config
	systemd-devel
	xmlto
	xorg-x11-proto-devel
)

infobegin "Update system"
dnf update -y
infoend

infobegin "Install dependency packages"
dnf install -y ${requires[@]}
infoend
