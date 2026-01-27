#!/usr/bin/bash

# Use grouped output messages
infobegin() {
	echo "::group::${1}"
}
infoend() {
	echo "::endgroup::"
}

# Required packages on Archlinux
requires=(
	ccache # Use ccache to speed up build
)

requires+=(
	autoconf-archive
	gcc
	git
	libmatekbd
	libnotify
	libxss
	make
	mate-common
	mate-desktop
	mate-menus
	mate-session-manager
	systemd
	which
)

infobegin "Update system"
pacman --noconfirm -Syu
infoend

infobegin "Install dependency packages"
pacman --noconfirm -S ${requires[@]}
infoend
