#!/bin/bash

set -e

# Needed for GCC 7 to work, see https://github.com/Microsoft/LightGBM/issues/1420
xcode-select --install

# Install newer GCC if we're running on GCC
if [ "${CXX}" == "g++" ]; then
    # We need to uninstall oclint because it creates a /usr/local/include/c++ symlink that clashes with the gcc5 package
    # see https://github.com/Homebrew/homebrew-core/issues/21172
    brew cask uninstall oclint
    brew install gcc@7
fi

brew cask install osxfuse
brew install libomp

# By default, travis only fetches the newest 50 commits. We need more in case we're further from the last version tag, so the build doesn't fail because it can't generate the version number.
git fetch --unshallow --tags

# Setup ccache
brew install ccache
