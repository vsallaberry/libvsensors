
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
This lib uses vlib (<https://github.com/vsallaberry/vlib>).

NOTE: This is a work in progress, this lib is not fully operational yet.

## System requirements
- A somewhat capable compiler (gcc/clang), make (GNU,BSD), sh (sh/bash/ksh)
  and coreutils (awk,grep,sed,date,touch,head,printf,which,find,test,...)

This is not an exhaustive list but the list of systems on which it has been built:
- Linux: slitaz 4 2.6.37, ubuntu 12.04 3.11.0, debian9.
- OSX 10.11.6
- OpenBSD 5.5
- FreeBSD 11.1

## Compilation
Make sure you clone the repository with '--recursive' option.  
    $ git clone --recursive https://github.com/vsallaberry/libvsensors

Just type:  
    $ make # (or 'make -j3' for SMP)

If the Makefile cannot be parsed by 'make', try:  
    $ ./make-fallback

Most of utilities used in Makefile are defined in variables and can be changed
with something like 'make SED=gsed TAR=gnutar' (or ./make-fallback SED=...)

To See how make understood the Makefile, you can type:  
    $ make info # ( or ./make-fallback info)

When making without version.h created (not the case for this repo), some old
bsd make can stop. Just type again '$ make' and it will be fine.

As libvsensors uses vlib, it should be linked with pthread (-lpthread),
and on linux, rt, dl (-lrt -ldl).

## Contact
[vsallaberry@gmail.com]  
<https://github.com/vsallaberry/libvsensors>

## License
GPLv3 or later. See LICENSE file.

CopyRight: Copyright (C) 2017-2019 Vincent Sallaberry

