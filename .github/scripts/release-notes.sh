#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  printf 'Usage: bash %s <vX.Y.Z> <owner/repo>\n' "$0" >&2
  exit 1
fi

release_tag="$1"
repository_url="${GITHUB_SERVER_URL:-https://github.com}/$2"
tag_pattern='^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
if [[ ! "$release_tag" =~ $tag_pattern ]]; then
  printf 'Release tags must use vX.Y.Z without leading zeroes.\n' >&2
  exit 1
fi
if [[ "$(git rev-parse --is-shallow-repository)" == true ]]; then
  printf 'Release notes require the full Git history and all tags (fetch-depth: 0).\n' >&2
  exit 1
fi

release_sha="$(git rev-parse --verify "refs/tags/$release_tag^{commit}")"
notes="$(git show "$release_sha:.github/RELEASE_TEMPLATE.md")"
minimum_hyprland="$(git show "$release_sha:CMakeLists.txt" | sed -nE 's/^[[:space:]]*set\(HYPRTHANOS_MIN_HYPRLAND_VERSION[[:space:]]+"([0-9]+\.[0-9]+\.[0-9]+)"\)[[:space:]]*$/\1/p')"
if [[ ! "v$minimum_hyprland" =~ $tag_pattern ]]; then
  printf 'Cannot read the minimum Hyprland version from the tagged CMakeLists.txt.\n' >&2
  exit 1
fi

notes="${notes//"{{VERSION}}"/"${release_tag#v}"}"
notes="${notes//"{{MIN_HYPRLAND_VERSION}}"/"$minimum_hyprland"}"
notes="${notes//"{{REPOSITORY_URL}}"/"$repository_url"}"
notes="${notes//"{{TAG}}"/"$release_tag"}"
if [[ "$notes" == *'{{'* || "$notes" == *'}}'* ]]; then
  printf 'The release template contains unresolved placeholders.\n' >&2
  exit 1
fi

previous_tag=''
tags="$(git tag --list --sort=version:refname)"
while IFS= read -r tag; do
  [[ "$tag" =~ $tag_pattern ]] || continue
  [[ "$tag" != "$release_tag" ]] || break
  previous_tag="$tag"
done <<< "$tags"

range="$release_sha"
changelog_url="$repository_url/commits/$release_tag"
if [[ -n "$previous_tag" ]]; then
  range="refs/tags/$previous_tag..$release_sha"
  changelog_url="$repository_url/compare/$previous_tag...$release_tag"
fi

commits="$(git log --no-color --reverse --topo-order --format='%H %s' "$range" --)"
printf '%s\n\n## Changes\n\n' "$notes"
if [[ -z "$previous_tag" ]]; then
  printf 'Initial release: all commits reachable from this tag are listed below.\n\n'
fi
if [[ -z "$commits" ]]; then
  printf 'No new commits.\n'
else
  while IFS=' ' read -r commit subject; do
    subject="$(printf '%s' "$subject" | LC_ALL=C sed 's/[[:punct:]]/\\&/g')"
    printf -- '- %s ([`%s`](%s/commit/%s))\n' "$subject" "${commit:0:7}" "$repository_url" "$commit"
  done <<< "$commits"
fi
printf '\n[Full Changelog](%s)\n' "$changelog_url"
