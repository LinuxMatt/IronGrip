IronGrip
========
Graphical frontend for Audio CD ripping and encoding

**In alpha stage, this project still needs lots of testing and bug fixes**.

Homepage
--------
https://github.com/LinuxMatt/IronGrip

Features
--------
- Supports WAV and MP3 encoding
- Uses CDDB

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

Requirements
------------
- GTK+ 2.6 or greater
- Libcddb 1.3.2 source code is included.
  http://libcddb.sourceforge.net/
- Cdparanoia (for Audio CD ripping).
  http://www.xiph.org/paranoia/
- LAME (for MP3 support).
  http://lame.sourceforge.net/

Installing requirements in Ubuntu
---------------------------------
To install Cdparanoia and Lame from a terminal, type the following commands:
- sudo apt-get install cdparanoia
- sudo apt-get install lame

Build dependencies
------------------
First of all, you will need the 'GNU toolchain' to build the project (gcc, make, autotools...).
To compile the source, you may also need the -dev or -devel versions of GTK+ and libcddb.
On Ubuntu, these packages are called libgtk2.0-dev and libcddb2-dev.
- sudo apt-get install automake
- sudo apt-get install intltool
- sudo apt-get install libgtk2.0-dev

Compiling
---------
First, you should install all the build dependencies.
Then run:
./maintainer-build.sh build

If you get errors, it's probably a missing GNU tool or dependency.
You can run the compiled program from the 'src' directory.

You should not use 'make install' for the moment, because it has not been thoroughly tested yet.
Coming soon ;)

Thanks
------
- Authors and contributors of the Asunder project. http://littlesvr.ca/asunder
- Authors and contributors of the Libcddb project. http://libcddb.sourceforge.net/

License
-------
The source code of this project is distributed under the GNU General Public Licence version 2.
See file 'COPYING'

The source code of libcddb is distributed under the GNU Library General Public Licence version 2.
See file 'libcddb/COPYING'

