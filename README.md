#
# libvsensors <https://github.com/vsallaberry/libvsensors>
# Copyright (C) 2017 Vincent Sallaberry
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# ############################################################################
#
# Generic Sensors Management library.
# (this lib uses vlib: <https://github.com/vsallaberry/vlib>).

## libvsensors
--------------

* [Overview](#overview)
* [System Requirments](#systemrequirments)
* [Compilation](#compilation)
* [Contact](#contact)
* [License](#license)

## Overview
**libvsensors** is a generic Sensors Management Library, including CPU, Memory, Network, SMC,
initially written for OSX (10.11.6).

## System requirements
- A somewhat capable compiler (gcc/clang), make (GNU,BSD), sh (sh/bash/ksh)
  and coreutils (awk,grep,sed,date,touch,head,printf,which,find,test,...)

This is not an exhaustive list but the list of systems on which it has been built:
- Linux: slitaz 4 2.6.37, ubuntu 12.04 3.11.0
- OSX 10.11.6
- OpenBSD 5.5
- FreeBSD 11.1

## Compilation
Just type:
    $ make

If the Makefile cannot be parsed by 'make', try:
    $ ./make-fallback

Most of utilities used in Makefile are defined in variables and can be changed
with something like 'make SED=gsed TAR=gnutar' (or ./make-fallback SED=...)

To See how make understood the Makefile, you can type:
    $ make info # ( or ./make-fallback info)

## Contact
[vsallaberry@gmail.com]

## License
GPLv3 or later. See LICENSE file.

