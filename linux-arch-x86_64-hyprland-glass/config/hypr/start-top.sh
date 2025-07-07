if command -v btop &> /dev/null; then
    btop
elif command -v htop &> /dev/null; then
    htop
else
    top
fi