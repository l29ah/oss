#!/bin/sh

VERSION=`sh showversion.sh`
RELEASE=`cat buildid.dat`
OSSNAME=oss-linux
PKGNAME=$OSSNAME-$VERSION-$RELEASE-`uname -i`

echo building $PKGNAME.tar.bz2
#cp ./setup/Linux/installoss.sh prototype
cp ./setup/Linux/removeoss.sh prototype/usr/lib/oss/scripts
(cd prototype; find . -type f -print > usr/lib/oss/MANIFEST)
(cd prototype; tar cfj /tmp/$PKGNAME.tar.bz2 . )
mv /tmp/$PKGNAME.tar.bz2 .

if test -f 4front-private/export_package.sh
then
  sh 4front-private/export_package.sh $PKGNAME.tar.bz2 . `sh showversion.sh` /tmp `uname -i`-26
fi

exit 0
