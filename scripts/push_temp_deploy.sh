#!/usr/bin/env bash
set -euo pipefail

remote=origin
main_branch=master
deploy_branch=temp/no-shop-map
skip_main_push=0
yes=0

usage() {
  cat <<'USAGE'
Usage: scripts/push_temp_deploy.sh [options]

Force-push master with lease, rebase the temporary deploy branch onto it, then
force-push the deploy branch with lease.

Options:
  -y, --yes              Skip the final force-push confirmation.
      --skip-master-push Do not force-push master before updating deploy branch.
      --remote NAME      Remote name. Default: origin.
      --master BRANCH    Source branch. Default: master.
      --deploy BRANCH    Deploy branch. Default: temp/no-shop-map.
  -h, --help             Show this help.
USAGE
}

die() {
  echo "error: $*" >&2
  exit 1
}

run() {
  echo "+ $*"
  "$@"
}

confirm() {
  local branch="$1"

  [[ "$yes" -eq 1 ]] && return 0
  [[ -t 0 ]] || die "refusing to force-push without a TTY; rerun with --yes if this is intentional"

  local answer
  read -r -p "Force-push ${remote}/${branch} with lease? [y/N] " answer
  [[ "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]] ||
    die "cancelled"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes)
      yes=1
      shift
      ;;
    --skip-master-push)
      skip_main_push=1
      shift
      ;;
    --remote)
      [[ $# -ge 2 ]] || die "--remote needs a value"
      remote="$2"
      shift 2
      ;;
    --master)
      [[ $# -ge 2 ]] || die "--master needs a value"
      main_branch="$2"
      shift 2
      ;;
    --deploy)
      [[ $# -ge 2 ]] || die "--deploy needs a value"
      deploy_branch="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

[[ -z "$(git status --porcelain)" ]] ||
  die "working tree is not clean; commit or stash your changes first"

current_branch="$(git symbolic-ref --quiet --short HEAD)" ||
  die "HEAD is detached; switch to ${main_branch} first"
[[ "$current_branch" == "$main_branch" ]] ||
  die "run this from ${main_branch}; current branch is ${current_branch}"

git show-ref --verify --quiet "refs/heads/${main_branch}" ||
  die "local branch ${main_branch} does not exist"

if [[ -d "$(git rev-parse --git-path rebase-merge)" ||
      -d "$(git rev-parse --git-path rebase-apply)" ||
      -f "$(git rev-parse --git-path MERGE_HEAD)" ]]; then
  die "a merge or rebase is already in progress"
fi

run git fetch "$remote" \
  "+refs/heads/${main_branch}:refs/remotes/${remote}/${main_branch}" \
  "+refs/heads/${deploy_branch}:refs/remotes/${remote}/${deploy_branch}"

if ! git show-ref --verify --quiet "refs/remotes/${remote}/${deploy_branch}"; then
  die "remote branch ${remote}/${deploy_branch} does not exist"
fi
if ! git show-ref --verify --quiet "refs/remotes/${remote}/${main_branch}"; then
  die "remote branch ${remote}/${main_branch} does not exist"
fi

if ! git show-ref --verify --quiet "refs/heads/${deploy_branch}"; then
  run git branch "$deploy_branch" "${remote}/${deploy_branch}"
fi

read -r deploy_local_only deploy_remote_only < <(
  git rev-list --left-right --count "${deploy_branch}...${remote}/${deploy_branch}"
)
if [[ "$deploy_local_only" -ne 0 || "$deploy_remote_only" -ne 0 ]]; then
  die "local ${deploy_branch} differs from ${remote}/${deploy_branch}; inspect it before rebasing"
fi

main_old="$(git rev-parse "refs/remotes/${remote}/${main_branch}")"
remote_old="$(git rev-parse "refs/remotes/${remote}/${deploy_branch}")"

if [[ "$skip_main_push" -eq 0 ]]; then
  main_new="$(git rev-parse "$main_branch")"
  echo
  echo "Remote main branch will move:"
  echo "  ${remote}/${main_branch}: ${main_old} -> ${main_new}"
  echo
  confirm "$main_branch"
  run git push --force-with-lease="refs/heads/${main_branch}:${main_old}" \
    "$remote" "${main_branch}:${main_branch}"
fi

run git switch "$deploy_branch"
run git rebase "$main_branch"

deploy_new="$(git rev-parse HEAD)"
echo
echo "Remote deploy branch will move:"
echo "  ${remote}/${deploy_branch}: ${remote_old} -> ${deploy_new}"
echo
echo "New deploy branch tip:"
run git log --oneline --decorate -n 5 HEAD
echo

confirm "$deploy_branch"
run git push --force-with-lease="refs/heads/${deploy_branch}:${remote_old}" \
  "$remote" "${deploy_branch}:${deploy_branch}"

run git switch "$main_branch"
echo "done: ${remote}/${deploy_branch} is rebased onto ${main_branch} and pushed"
