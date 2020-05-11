#!/bin/bash
qmake we-comm.pro -spec macx-clang CONFIG+=debug CONFIG+=x86_64 CONFIG+=qml_debug && \
make clean && \
make
