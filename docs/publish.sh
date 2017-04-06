#!/bin/bash

set -e

DOCS_DIR=generated/docs
PUBLISH_DIR=../envoy-docs
BUILD_SHA=`git rev-parse HEAD`

if [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_BRANCH" == "master" ]
then
  # .publishdocskey.enc is an encoded ssh key used to push to our gh-pages branch.
  # travis encrypted the key, and set up two secret variables to decrypt the key:
  # $encrypted_b1a4cc52fa4a_key, and $encrypted_b1a4cc52fa4a_iv. The command
  # below uses those vars to decrypt the key.
  openssl aes-256-cbc -K $encrypted_b1a4cc52fa4a_key -iv $encrypted_b1a4cc52fa4a_iv -in .publishdocskey.enc -out .publishdocskey -d

  # Set up ssh config to be able to push the docs to github.
  echo "Setting up ssh"
  mkdir -p ~/.ssh
  # point to the ssh key and disable strict host checking.
  echo -e "Host github.com\n\tHostName github.com\n\tUser git\n\tIdentityFile .publishdocskey\n\tStrictHostKeyChecking no\n" >> ~/.ssh/config

  echo "Setting up Git Access"
  chmod 600 .publishdocskey

  # Add the SSH key so it's used on git commands
  eval `ssh-agent -s`
  ssh-add .publishdocskey

  echo 'cloning'
  git clone git@github.com:lyft/envoy $PUBLISH_DIR

  git -C $PUBLISH_DIR fetch
  git -C $PUBLISH_DIR checkout -B gh-pages origin/gh-pages
  rm -fr $PUBLISH_DIR/*
  cp -r $DOCS_DIR/* $PUBLISH_DIR
  cd $PUBLISH_DIR

  git config user.name "lyft-buildnotify(travis)"
  git config user.email lyft-buildnotify@users.noreply.github.com
  echo 'add'
  git add .
  echo 'commit'
  git commit -m "docs @$BUILD_SHA"
  echo 'push'
  git push origin gh-pages
else
  echo "Ignoring PR branch for docs push"
fi
