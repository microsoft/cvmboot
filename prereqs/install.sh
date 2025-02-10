#!/bin/bash
OPT=DEBIAN_FRONTEND=noninteractive

#==============================================================================
# Development only dependencies:
#==============================================================================
sudo ${OPT} apt install -y make
sudo ${OPT} apt install -y gcc
sudo ${OPT} apt install -y g++
sudo ${OPT} apt install -y figlet
sudo ${OPT} apt install -y libfuse3-dev
sudo ${OPT} apt install -y tpm2-tools
sudo ${OPT} apt install -y pkg-config
sudo ${OPT} apt install -y libssl-dev
sudo ${OPT} apt install -y bzip2
if [ ! -d "${HOME}/.cargo/bin" ];
then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
fi

#==============================================================================
# Install dependencies:
#==============================================================================
sudo ${OPT} apt install -y pv
