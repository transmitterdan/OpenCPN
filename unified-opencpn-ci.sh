#!/usr/bin/env bash
set -euo pipefail

# =====================================================================
#  Unified OpenCPN CI Helper (Git Bash / SSH)
#
#  PURPOSE:
#    Automates two tasks for a fork of OpenCPN:
#      1) Trigger CI for current feature branch by force-pushing it to origin/master.
#      2) Restore real master by syncing with upstream/master and force-pushing to origin/master.
#
#    Updates GitHub Actions secret CLOUDSMITH_REPO so artifacts go to the
#    correct Cloudsmith repo for each mode.
#
#  REQUIREMENTS:
#    - GitHub CLI installed and authenticated: gh auth login --git-protocol ssh
#    - SSH keys configured and working with GitHub
#    - Remotes: origin=fork, upstream=official OpenCPN repo
# ======================================================================
#  How to create SSH key and tell github.com how to use it:
#
#  1) In git bash do this:
#     > ssh-keygen -t ed25519 -C "COMMENT"
#           COMMENT is typically your email address associated with GitHub
#     > eval "$(ssh-agent -s)"
#     > ssh-add ~/.ssh/id_ed25519
#     > cat ~/.ssh/id_ed25519.pub | clip
#       This step has put the public key string into the clipboard
#  2) Go to your github web repository and open account Settings>SSH and GPG keys
#     Account settings are access via your avatar in the upper right corner.
#     Then select New SSH key button
#     Paste in the key from the clipboard. It should look something like this:
#     ssh-ed25519 ABCDE3NzaC1lZDI1NTE5ABCDEGHIJKLMNOPQRSTUVwX/abcdefghigklmu1235664211 yourname@gmail.com
#     Click the save key button to store the public key in github.com. It will associated
#     this key with the repository you were in when you clicked your avatar.
#  3) Test the key with this command in git bash window:
#     > ssh -T git@github.com
#     You should see something like: "Hi username! and a message. You may have to answer some one time
#     questions from the SSH login process.
# =====================================================================

usage() {
  cat <<'EOF'
Usage:
  ./unified-opencpn-ci.sh ci
      Push current branch to origin/master (force) and set CLOUDSMITH_REPO
      to dan-dickey/opencpn-unstable-testing

  ./unified-opencpn-ci.sh restore
      Restore master from upstream/master and set CLOUDSMITH_REPO
      to dan-dickey/opencpn-github
EOF
}

require_cmds() {
  for cmd in git gh; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "Missing required command: $cmd" >&2; exit 1; }
  done
}

run_ci() {
  echo "------------------------------------------------------------"
  echo " OPERATION: Trigger CI by pushing current branch to origin/master"
  echo "------------------------------------------------------------"

  current_branch=$(git rev-parse --abbrev-ref HEAD)
  echo "Current branch detected: $current_branch"
  echo

  echo "Setting CLOUDSMITH_REPO for CI mode..."
  gh secret set CLOUDSMITH_REPO --repo transmitterdan/OpenCPN --body "dan-dickey/opencpn-unstable-testing"
  echo "CLOUDSMITH_REPO updated for CI mode."
  echo

  echo "Pushing $current_branch to origin/master --force..."
  git push origin "$current_branch":master --force
  echo
  echo "CI trigger push complete."
}

restore_master() {
  echo "------------------------------------------------------------"
  echo " OPERATION: Restore real master from upstream/master"
  echo "------------------------------------------------------------"

  current_branch=$(git rev-parse --abbrev-ref HEAD)
  echo "Current branch detected: $current_branch"
  echo

  echo "Setting CLOUDSMITH_REPO for restore mode..."
  gh secret set CLOUDSMITH_REPO --repo transmitterdan/OpenCPN --body "dan-dickey/opencpn-github"
  echo "CLOUDSMITH_REPO updated for restore mode."
  echo

  echo "Checking out local master..."
  git checkout master

  echo "Fetching upstream..."
  git fetch upstream

  echo "Resetting local master to upstream/master..."
  git reset --hard upstream/master

  echo "Force pushing clean master back to origin/master..."
  git push origin master --force

  echo "Going back to $current_branch..."
  git checkout "$current_branch"

  echo
  echo "Master successfully restored."
}

main() {
  require_cmds
  case "${1-}" in
    ci) run_ci ;;
    restore) restore_master ;;
    *) usage; exit 1 ;;
  esac
}

main "$@"
