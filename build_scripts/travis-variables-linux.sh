# variables necessary for travis, linux

check_var "CONDA_PREFIX" "Require the CASM development environment is activated"

CASM_PREFIX=$CONDA_PREFIX
export CASM_BOOST_PREFIX=$CASM_PREFIX

export CASM_BASH_COMPLETION_DIR=$CONDA_PREFIX/.bash_completion.d
export CASM_BASH_COMPLETION=/usr/share/bash-completion/bash_completion

CASM_CONFIGFLAGS="--prefix=$CASM_PREFIX "
CASM_CONFIGFLAGS+="--with-zlib=$CASM_PREFIX "
CASM_CONFIGFLAGS+="--with-boost-libdir=$CASM_BOOST_PREFIX/lib "
CASM_CONFIGFLAGS+="--with-bash-completion-dir=$CASM_BASH_COMPLETION_DIR "
export CASM_CONFIGFLAGS

export CASM_CXXFLAGS="-O3 -Wall -fPIC --std=c++11 -DNDEBUG -Wno-deprecated-register -Wno-ignored-attributes -Wno-deprecated-declarations"
export CASM_CC="$CC"
export CASM_CXX="$CXX"
