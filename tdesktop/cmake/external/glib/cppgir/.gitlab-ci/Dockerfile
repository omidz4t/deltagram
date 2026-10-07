FROM ubuntu:noble

RUN apt-get update
RUN apt-get -y --no-install-recommends install \
    build-essential cmake libboost-program-options-dev libboost-fiber-dev \
    libfmt-dev ronn clang-18 clang-tools-18 \
    libgirepository1.0-dev libgstreamer1.0-dev \
    libgtk-3-dev gir1.2-gtk-3.0 \
    libgtk-4-dev gir1.2-gtk-4.0 \
    qtbase5-dev meson git
RUN apt-get clean

WORKDIR /build

ENV LANG C.UTF-8
