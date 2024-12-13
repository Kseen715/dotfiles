#!/bin/sh

# Install ./.zshrc to $HOME
# if file exists, ask to overwrite
if [ -f $HOME/.zshrc ]; then
    echo "File .zshrc already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp ./.zshrc $HOME/.zshrc
    fi
else
    cp ./.zshrc $HOME/.zshrc
fi

# Install ./.oh-my-zsh to $HOME
# if directory exists, ask to overwrite
if [ -d $HOME/.oh-my-zsh ]; then
    echo "Directory .oh-my-zsh already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp -r ./.oh-my-zsh $HOME/.oh-my-zsh
    fi
else
    cp -r ./.oh-my-zsh $HOME/.oh-my-zsh
fi