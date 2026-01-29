#!/usr/bin/bash

set -eo pipefail

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
	clang  # Build with clang on Archlinux
)

# https://gitlab.archlinux.org/archlinux/packaging/packages/mate-screensaver
requires+=(
	autoconf-archive
	gcc
	gettext
	git
	glib2-devel
	libmatekbd
	libnotify
	libxss
	make
	mate-common
	mate-desktop
	mate-menus
	mate-panel
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
