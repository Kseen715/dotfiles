#!/bin/bash

sudo apt update \
&& sudo apt install curl zsh \
&& (curl -sS https://starship.rs/install.sh | sh)
