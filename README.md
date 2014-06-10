IronGrip
========
Graphical frontend for Audio CD ripping and encoding.

Features
--------
- Supports WAV, MP3, Ogg Vorbis and FLAC encoding
- Uses CDDB
- Finds cdrom drives
- Compilation and installation from source is so easy ;)
[![Build Status](https://travis-ci.org/LinuxMatt/IronGrip.png?branch=master)](https://travis-ci.org/LinuxMatt/IronGrip)

Binary packages
---------------
You can download debian packages at http://sourceforge.net/projects/irongrip/.
- They are compatible with Ubuntu 10.04 and above.
- They are compatible with Linux Mint 9 and above.
- They should also work on Debian 6 and above.

News, help, bugs, suggestions and feedback
------------------------------------------
- For news about releases, go to our project page at http://freecode.com/projects/irongrip
- For bugs and requests, open an issue at https://github.com/LinuxMatt/IronGrip/issues
- To contact me, send an e-mail to <matt59491@gmail.com>

This project is still young, it *needs your feedback* ;)

Requirements
------------
- Linux
- GTK+ 2.12 or greater
- Glib 2.16 or greater
- Libcddb 1.3.2 source code is included.
  http://libcddb.sourceforge.net/
- Cdparanoia (for Audio CD ripping).
  http://www.xiph.org/paranoia/
- LAME (for MP3 support).
  http://lame.sourceforge.net/
- FLAC (for FLAC encoding).
  http://flac.sourceforge.net/
- Vorbis tools (for Ogg Vorbis encoding)
  http://vorbis.com/

Installing requirements in Ubuntu and Debian
--------------------------------------------
To install Cdparanoia, Lame and flac from a terminal, type the following commands:
- sudo apt-get install cdparanoia
- sudo apt-get install lame
- sudo apt-get install flac
- sudo apt-get install vorbis-tools

Build dependencies
------------------
First of all, you will need the 'GNU toolchain' to build the project (gcc, make, autotools...).
To compile the sources, you may also need the -dev or -devel version of GTK+.

On Ubuntu, you can just run the following commands to install all requirements:
- sudo apt-get install dh-make
- sudo apt-get install libgtk2.0-dev
- sudo apt-get install intltool

On Fedora, you may have to install gtk+2-devel:
- yum install gtk+2-devel

Compiling
---------
First, you should install all the build dependencies.
Then, from the top directory, run:
./easy-make.sh build

If you get errors, it's probably a missing GNU tool or dependency.
You can run the compiled program from the 'src' subdirectory.

Supported systems
-----------------
The following distributions are expected to work with IronGrip:
- Ubuntu 8.04 LTS and above (32bit and 64bit)
- Any Ubuntu derivative of version 8.04 and above (e.g. Linux Mint 5 and up)
- Fedora 10 and above 
- All Linux distributions with the above requirements (not tested yet)

Test systems
------------
The following distributions are used for developing IronGrip:
- Ubuntu 8.04.4 LTS
- Fedora 10
- Ubuntu 10.04.4 LTS (32 bit and 64 bit), used also for creating packages.
- Ubuntu 12.04 LTS / Linux Mint 13 LTS
- Ubuntu 13.10 / Linux Mint 16

Homepage
--------
https://github.com/LinuxMatt/IronGrip

History
-------
IronGrip is a stripped-down (and experimental) fork of the Asunder project.
The Asunder program is a graphical Audio CD ripper and encoder for Linux.
Asunder's project homepage : http://littlesvr.ca/asunder .
I would like to thank the authors and contributors of the Asunder project for sharing their work under GPL.
If you're looking for a stable audio CD ripper and encoder with lots of encoding formats, then you should use Asunder instead of IronGrip.
Asunder's packages are available for several linux distributions.

Authors
-------
- Matt Thirtytwo <matt59491@gmail.com>
- Andrew Smith (Asunder)
- Eric Lathrop (Asunder)
- And the contributors of the Asunder project.

Thanks
------
- Authors and contributors of the Asunder project. http://littlesvr.ca/asunder
- Authors and contributors of the Libcddb project. http://libcddb.sourceforge.net/
- Michael McTernan for RipRight. http://www.mcternan.me.uk/ripright/

License
-------
The source code of this project is distributed under the GNU General Public Licence version 2.
See file 'COPYING'

The source code of libcddb is distributed under the GNU Library General Public Licence version 2.
See file 'libcddb/COPYING'

