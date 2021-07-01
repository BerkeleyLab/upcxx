#!/bin/bash

set -e

REPO=${REPO:-origin}
BRANCH=${BRANCH:-master}

REPOURL=$(git remote get-url $REPO)
RELEASE=${RELEASE:-$(git describe ${REPO}/${BRANCH})}

if [[ -e ${RELEASE} ]]; then
  echo "ERROR: refusing to overwrite ${RELEASE}."
  exit 1
fi

# Indirection to deal w/ Linux vs macOS differences
if [[ $(uname -s) = 'Darwin' ]]; then
  MD5_CMD='md5 -r'
  WWW_CMD='curl -LOsS'
else
  MD5_CMD='md5sum'
  WWW_CMD='wget -q'
fi

echo "Building release from BRANCH=$BRANCH in REPO=$REPO ($REPOURL)"

set -x

# Download UPC++ repo:
RELEASE_TGZ=${RELEASE}.tar.gz
git archive --remote=${REPO} ${BRANCH} --prefix=${RELEASE}/ --format=tar.gz --output=${RELEASE_TGZ}
if ! gzip -t ${RELEASE_TGZ}; then
  echo 'ERROR: release tarball failed "gzip -t"' >&2
  exit 1
fi
UPCXX_HASH=$(zcat ${RELEASE_TGZ} | git get-tar-commit-id)

# Extract GASNet-EX URL:
GASNET_URL=${GASNET_URL:-$(tar xOfz ${RELEASE_TGZ} ${RELEASE}/configure | grep -v '^ *#' | grep 'http.*GASNet-.*\.tar\.gz' | tail -n 1 | cut -d\' -f2)}
GASNET=$(basename "${GASNET_URL}" .tar.gz)

# Downoad GEX and extract info
mkdir -p ${RELEASE}/src
(cd ${RELEASE}/src && $WWW_CMD "${GASNET_URL}")
GASNET_TGZ=${RELEASE}/src/${GASNET}.tar.gz
GEX_MD5SUM="$( $MD5_CMD ${GASNET_TGZ} | cut -d\  -f1 )"
if ! gzip -t $GASNET_TGZ; then
  echo 'ERROR: GASNet-EX tarball failed "gzip -t"' >&2
  exit 1
fi
# Rename GEX tarball to full version name
GASNET=$(tar tf $GASNET_TGZ | head -1 | cut -d/ -f1)
GASNET_TGZ_NEW=${RELEASE}/src/${GASNET}.tar.gz
if test "$GASNET_TGZ" != "$GASNET_TGZ_NEW" ; then
  mv $GASNET_TGZ $GASNET_TGZ_NEW
  GASNET_TGZ=$GASNET_TGZ_NEW
fi

# Insert GEX tarball into archive
GEX_DESCRIBE=$(tar xOzf ${GASNET_TGZ} ${GASNET}/version.git)
gunzip ${RELEASE_TGZ}
tar -r -f ${RELEASE}.tar --owner=root --group=root ${GASNET_TGZ}
gzip -9f ${RELEASE}.tar
if ! gzip -t ${RELEASE_TGZ}; then
  echo 'ERROR: release tarball failed "gzip -t"' >&2
  exit 1
fi
rm -Rf ${RELEASE}

set +x

echo
echo "INPUT SUMMARY:"
echo "-------------"
echo "Repo:               $REPO ($REPOURL)"
echo "Branch:             $BRANCH"
echo "Release version:    ${RELEASE#upcxx-}"
echo "UPC++ commit hash:  ${UPCXX_HASH}"
echo "GASNet-EX describe: ${GEX_DESCRIBE}"
echo "GASNet-EX checksum: ${GEX_MD5SUM}"
echo
echo "OUTPUT SUMMARY:"
echo "--------------"
$MD5_CMD ${RELEASE_TGZ}

