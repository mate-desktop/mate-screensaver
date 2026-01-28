#!/usr/bin/bash

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Debian
requires=(
	ccache # Use ccache to speed up build
)

requires+=(
	autoconf-archive
	autopoint
	gcc
	git
	libdbus-glib-1-dev
	libdconf-dev
	libglib2.0-dev
	libgtk-3-dev
	libmate-desktop-dev
	libmate-menu-dev
	libmatekbd-dev
	libnotify-dev
	libpam0g-dev
	libstartup-notification0-dev
	libsystemd-dev
	libx11-dev
	libxext-dev
	libxss-dev
	libxt-dev
	libxxf86vm-dev
	make
	mate-common
	x11proto-scrnsaver-dev
	x11proto-xext-dev
	x11proto-xf86vidmode-dev
	xmlto
)

infobegin "Update system"
apt-get update -qq
infoend

infobegin "Install dependency packages"
env DEBIAN_FRONTEND=noninteractive \
	apt-get install --assume-yes \
	${requires[@]}
infoend
