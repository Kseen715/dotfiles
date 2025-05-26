#!/bin/bash

sudo apt update \
&& sudo apt install curl zsh \
&& (curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh) \
&& echo "Rust installed. Opening new shell..." \
&& exec zsh -c ". \$HOME/.cargo/env source && cargo install starship --locked && ./install.sh"
