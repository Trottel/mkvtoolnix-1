#!/bin/bash

set -e

# Creates a tree with all the required libraries for use with the
# mingw cross compiler. The libraries are compiled appropriately.
# Read the file "README.Windows.md" for instructions.

#
# SETUP -- adjust these variables if neccessary.
# You can also override them from the command line:
# INSTALL_DIR=/opt/mingw ./setup_cross_compilation_env.sh
#

# This defaults to a 64bit executable. If you need a 32bit executable
# then change ARCHITECTURE to 32.
ARHICTECTURE=64
# Installation defaults to ~/mingw-cross-env.
INSTALL_DIR=${INSTALL_DIR:-$HOME/mingw-cross-env}
# Leave PARALLEL empty if you want the script to use all of your CPU
# cores.
PARALLEL=${PARALLEL:-$(( $(awk '/^core id/ { print $4 }' /proc/cpuinfo | sort | tail -n 1) + 2 ))}

#
# END OF SETUP -- usually no need to change anything else
#

if [[ "$ARCHITECTURE" == 32 ]]; then
  HOST=i686-w64-mingw32.static
else
  HOST=x86_64-w64-mingw32.static
fi

SRCDIR=$(pwd)
LOGFILE=$(mktemp -p '' mkvtoolnix_setup_cross_compilation_env.XXXXXX)

function update_mingw_cross_env {
  if [[ ! -d $INSTALL_DIR ]]; then
    echo Retrieving the mingw-cross-env build scripts >> $LOGFILE
    git clone https://github.com/mbunkus/mxe $INSTALL_DIR >> $LOGFILE 2>&1
  else
    echo Updating the mingw-cross-env build scripts >> $LOGFILE
    cd $INSTALL_DIR
    git pull >> $LOGFILE 2>&1
  fi

  cd ${INSTALL_DIR}
  cat > settings.mk <<EOF
MXE_TARGETS=${HOST}
JOBS=${PARALLEL}

mkvtoolnix-deps:
	+make gettext libiconv zlib boost curl file flac lzo ogg pthreads vorbis qtbase qttranslations qtwinextras
EOF
}

function create_run_configure_script {
  cd $SRCDIR

  echo Creating \'run_configure.sh\' script
  qtbin=${INSTALL_DIR}/usr/${HOST}/qt5/bin
  cat > run_configure.sh <<EOF
#!/bin/bash

export PATH=${INSTALL_DIR}/usr/bin:$PATH
hash -r

./configure \\
  --host=${HOST} \\
  --with-boost="${INSTALL_DIR}/usr/${HOST}" \\
  --enable-qt --enable-static-qt --with-mkvtoolnix-gui \\
  --with-moc=${qtbin}/moc --with-uic=${qtbin}/uic --with-rcc=${qtbin}/rcc \\
  "\$@"

exit \$?
EOF
  chmod 755 run_configure.sh

  if [[ ! -f configure ]]; then
    echo Creating \'configure\'
    ./autogen.sh
  fi
}

function configure_mkvtoolnix {
  cd $SRCDIR

  echo Running configure.
  set +e
  ./run_configure.sh >> $LOGFILE 2>&1
  local result=$?
  set -e

  echo
  if [ $result -eq 0 ]; then
    echo 'Configuration went well. Congratulations. You can now run "drake"'
    echo 'after adding the mingw cross compiler installation directory to your PATH:'
    echo '  export PATH='${INSTALL_DIR}'/usr/bin:$PATH'
    echo '  hash -r'
    echo '  ./drake'
  else
    echo "Configuration failed. Look at ${LOGFILE} as well as"
    echo "at ./config.log for hints as to why."
  fi

  echo
  echo 'If you need to re-configure MKVToolNix then you can do that with'
  echo 'the script ./run_configure.sh. Any parameter you pass to run_configure.sh'
  echo 'will be passed to ./configure as well.'
}

function build_libraries {
  echo Building the cross-compiler and the required libraries
  cd ${INSTALL_DIR}
  make mkvtoolnix-deps >> $LOGFILE 2>&1
}

# main

echo "Cross-compiling MKVToolNix. Log output can be found in ${LOGFILE}"
update_mingw_cross_env
build_libraries
create_run_configure_script
configure_mkvtoolnix
