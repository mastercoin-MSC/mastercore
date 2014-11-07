Master Core Upstart support for Ubuntu (tested on 14.04 LTS)

To install:

1. Build Master Core

        ./autogen.sh
        ./configure
        make

1. Run the install script

        sudo ./contrib/msc-ubuntu/install-mastercore-upstart.sh

Running `mastercore-cli` for a quick test:

1. Add your user to the `bitcoin` group so you can read `/etc/bitcoin/bitcoin.conf`

        sudo usermod -a -G bitcoin <username>

1. Edit /etc/bitcoin/bitcoin.conf and uncomment and set `rpcuser` and `rpcpassword`.

1. Start the `mastercored` service.

        sudo service mastercored start

1. Use the `getinfo` RPC to make sure the daemon and RPC is working

        /usr/sbin/mastercore-cli -conf=/etc/bitcoin/bitcoin.conf getinfo


This is based upon upstream work that is documented in these links:

* [Issue #4124](https://github.com/bitcoin/bitcoin/issues/4124)
* [Issue #4611](https://github.com/bitcoin/bitcoin/pull/4611)

This branch/PR used `git cherry-pick` to pull some of that code so it can be added to the `michael-0921` branch of Master Core.

Doc on the files from the "Cherry Pick" can be found:

* [init/README.md](../init/README.md)
* [doc/init.md](../../doc/init.md)

It has been tested on Ubuntu 14.04 LTS.

There is a Vagrant script to install it here:
[https://github.com/msgilligan/install-msc/tree/vagrant](https://github.com/msgilligan/install-msc/tree/vagrant)


