![wavemon screenshot](https://cloud.githubusercontent.com/assets/5132989/8640926/1f8436a0-28c6-11e5-9336-a79fd002c324.png)

## Synopsis

wavemon is a wireless device monitoring application that allows you to watch
signal and noise levels, packet statistics, device configuration and network
parameters of your wireless network hardware. It should work (though with
varying features) with all devices supported by the Linux kernel.

See the man page for an in-depth description of operation and configuration.


### Where to obtain

Apart from debian/ubuntu packages (`apt-cache search wavemon`) and [slackbuild  scripts for wavemon](https://slackbuilds.org/result/?search=wavemon&sv=), this repository contains the full source code.

Please check [this page](#) for updates and for further information.

wavemon is distributed under the [GPLv3](http://www.gnu.org/licenses/gpl-3.0.en.html), refer to the file `COPYING`.

### Dependencies

wavemon requires a Linux Kernel with wireless extensions enabled. If your Kernel setup uses `CONFIG_CFG80211`, make sure that the config option `CONFIG_CFG80211_WEXT` is set.

In addition, minimally the following are required:
* Netlink `libnl` at least version 3.2,
* including the Generic Netlink support (`libnl-genl`),
* ncurses development files (`libncurses5-dev`),
* the `pkg-config` package.

On Debian/Ubuntu, this can be done using
```bash
	apt-get -y install pkg-config libncurses5-dev libnl-3-dev libnl-genl-3-dev
```

## How to build

wavemon uses `autoconf`, so that in most cases you can simply run
```
	./configure
	make
	sudo make install
```
to build and install the package. Type 'make uninstall' if not happy.
Refer to the file `INSTALL` for generic installation instructions.

To grant users access to restricted networking operations (scan operations), use additionally
```
	sudo make install-suid-root
```
If you have changed some of the autoconf files or use a git version, run
```
	./config/bootstrap
```
(This requires a recent installation of `autotools`.)

## Bugs?

Send bug reports, comments, and suggestions by opening an issue on [github](https://github.com/uoaerg/wavemon/issues).

## Dependency issues while building under some versions of Raspbian
There maybe some problems while installing the dev package to build the prgram. Here are steps to resolve:

Install the dev package ignoring the dependencies version issues

```
apt-get download libncurses5-dev libnl-3-dev libnl-genl-3-dev

sudo dpkg -i --ignore-depends=libtinfo5,libncurses5,libtinfo-dev,ncurses-bin libncurses5-dev_5.9-10_armhf.deb

sudo dpkg -i --ignore-depends=libnl-3-200 libnl-3-dev_3.2.7-4+deb7u1_armhf.deb

sudo dpkg -i --ignore-depends=libnl-genl-3-200 libnl-genl-3-dev_3.2.7-4+deb7u1_armhf.deb

rm -f libncurses5-dev_5.9-10_armhf.deb libnl-3-dev_3.2.7-4+deb7u1_armhf.deb libnl-genl-3-dev_3.2.7-4+deb7u1_armhf.deb

```

Recreate the symbol link 

```
cd /lib/arm-linux-gnueabihf/

sudo ln -s -f libnl-3.so.2000.19.0 libnl-genl-3.so

sudo ln -s -f libnl-3.so.200.19.0 libnl-3.so

sudo ldconfig

```
