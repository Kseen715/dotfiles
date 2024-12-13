#!/bin/sh

# Save $HOME/.zshrc to current directory
# if file exists, ask to overwrite
if [ -f ./.zshrc ]; then
    echo "File .zshrc already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp $HOME/.zshrc ./.zshrc
    fi
else
    cp $HOME/.zshrc ./.zshrc
fi

# Save $HOME/.oh-my-zsh to current directory
# if directory exists, ask to overwrite
if [ -d ./.oh-my-zsh ]; then
    echo "Directory .oh-my-zsh already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp -r $HOME/.oh-my-zsh ./.oh-my-zsh
    fi
else
    cp -r $HOME/.oh-my-zsh ./.oh-my-zsh
fi