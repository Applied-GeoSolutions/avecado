#!/bin/bash

set -e # Die immediately on error

# Define colors.
CYAN='\033[0;36m'
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# Check that the supplied prefix is a valid directory.
if [ -z $1 ] || [ ! -d $1 ]; then
  printf "${RED}Please specify a valid installation prefix.${NC}\n"
  exit 1
fi

PREFIX=$(readlink -e $1) # Use readlink to portably get fully qualified dir.

TMP_DIR=${PWD}/.build_tmp

# Cleanup function to run on any exit, good or bad.
function cleanup {
  local exit_status=$?

  printf "${CYAN}Cleaning up...${NC}\n\n"
  rm -rf ${TMP_DIR}

  if [ "$exit_status" = '0' ]; then
    printf "${GREEN}Installation complete!${NC}\n"
  else
    printf "${RED}Installation aborted due to error.${NC}\n"
  fi
}

trap cleanup EXIT

# Retrieve all of the necessary code to build.
printf "${CYAN}Retrieving boost 1.59.0...${NC}\n\n"
wget --directory-prefix=${TMP_DIR} http://sourceforge.net/projects/boost/files/boost/1.59.0/boost_1_59_0.tar.gz

printf "${CYAN}Retrieving mapnik...${NC}\n\n"
git clone https://github.com/mapnik/mapnik ${TMP_DIR}/mapnik

printf "${CYAN}Retrieving avecado...${NC}\n\n"
git clone https://github.com/MapQuest/avecado ${TMP_DIR}/avecado


# Expand boost.
printf "${CYAN}Expanding boost...${NC}\n\n"
tar xzf ${TMP_DIR}/boost_1_59_0.tar.gz -C ${TMP_DIR}

# Build boost.
printf "${CYAN}Building boost libraries...${NC}\n\n"
cd ${TMP_DIR}/boost_1_59_0
./bootstrap.sh --prefix=${PREFIX} --with-libraries=filesystem,system,regex,program_options,thread,python,iostreams,date_time
./b2 install

# Build mapnik.
printf "${CYAN}Building mapnik...${NC}\n\n"
cd ${TMP_DIR}/mapnik
git checkout 7ee9745a8fe6e429316bf8d7d58657db87f96cc2
./configure PREFIX=${PREFIX} BOOST_INCLUDES=${PREFIX}/include BOOST_LIBS=${PREFIX}/lib
make
make install

# Build avecado.
printf "${CYAN}Building avecado...${NC}\n\n"
cd ${TMP_DIR}/avecado
git submodule update --init --recursive
./autogen.sh
export CPPFLAGS="-DBOOST_MPL_CFG_NO_PREPROCESSED_HEADERS -DBOOST_MPL_LIMIT_VECTOR_SIZE=30"
./configure --prefix=${PREFIX} --with-boost=${PREFIX} --with-mapnik-config=${PREFIX}/bin/mapnik-config
make
make install
