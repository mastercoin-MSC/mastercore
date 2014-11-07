#!/bin/sh

BITCOIND_BIN_NAME="mastercored"
BITCOINCLI_BIN_NAME="mastercore-cli"
SERVICE_NAME="mastercored"

# Copy the executable
cp src/$BITCOIND_BIN_NAME /usr/bin/$BITCOIND_BIN_NAME
cp src/$BITCOINCLI_BIN_NAME /usr/bin/$BITCOINCLI_BIN_NAME

# Set up directories, users, and permissions as standardized by Bitcoin Core
mkdir -p /etc/bitcoin
mkdir -p /var/lib/bitcoind
mkdir -p /var/run/bitcoind

groupadd --force --system  bitcoin
useradd -M --system --shell /usr/sbin/nologin --home /var/lib/bitcoind --gid bitcoin bitcoin

chown bitcoin:bitcoin /var/lib/bitcoind
chmod 750 /var/lib/bitcoind

chown bitcoin:bitcoin /var/run/bitcoind
chmod 755 /var/run/bitcoind

# Copy Bitcoin configuration file
cp contrib/msc-ubuntu/bitcoin.conf /etc/bitcoin
chown bitcoin:bitcoin /etc/bitcoin/bitcoin.conf
chmod 660 /etc/bitcoin/bitcoin.conf

# Copy Upstart configuration file/script
cp contrib/msc-ubuntu/mastercored.conf /etc/init/$SERVICE_NAME.conf

echo "Set rpcuser and rpcpassword in /etc/bitcoin/bitcoin.conf, then type: 'service $SERVICE_NAME start' (as root/sudo)"


