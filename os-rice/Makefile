# Makefile -- TESTING CONVENIENCE ONLY. Not the build system.
#
# nob.c is the build system (see its header comment and PLAN_UNIVERSAL.md
# decision 6: the C core must build with nothing but a C compiler, because
# `make` on Windows is one more thing to install and PATH-manage). Nothing
# in this repo -- not osr, not osr.ps1/osr.bat, not CI -- calls this file,
# and nothing may start to: it exists so a developer already sitting in a
# shell with make can type `make` / `make test` out of habit.
#
# Every target just bootstraps nob and forwards to it. Delete this file and
# the build still works exactly the same; delete nob.c and nothing does.

CC ?= cc
# .exe on Windows (where make comes from a mingw/msys shell), bare elsewhere.
ifeq ($(OS),Windows_NT)
NOB ?= ./nob.exe
else
NOB ?= ./nob
endif

.PHONY: all test clean

all: $(NOB)
	$(NOB)

test: $(NOB)
	$(NOB) test

clean: $(NOB)
	$(NOB) clean

# The one-time bootstrap from nob.c's header. After this, nob rebuilds
# itself whenever nob.c changes (NOB_GO_REBUILD_URSELF), so this rule
# effectively never runs again.
$(NOB): nob.c nob.h
	$(CC) -o $(NOB) nob.c
