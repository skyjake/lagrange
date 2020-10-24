#!/bin/sh -xv

apt-get update -qq -y
apt-get install -y -qq --no-install-recommends cmake libsdl2-dev libssl-dev libpcre3-dev zlib1g-dev libunistring-dev libmpg123-dev debhelper dh-make devscripts fakeroot git build-essential
git submodule sync

git archive --format=tar.gz --prefix=lagrange-${RELEASE_VERSION}/ HEAD >lagrange-${RELEASE_VERSION}.tar.gz
tar -xvzf lagrange-${RELEASE_VERSION}.tar.gz
ln -s lagrange-${RELEASE_VERSION}.tar.gz lagrange_${RELEASE_VERSION}.orig.tar.gz 
cd lagrange-${RELEASE_VERSION}
debuild
cd ..
mkdir -p artifacts
mv *deb artifacts/

