#!/usr/bin/env bash
# Build source and binary RPMs from the current checkout.
set -euo pipefail

REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPEC_FILE="${REPOSITORY_ROOT}/packaging/scalpel-editor.spec"
RPM_TOPDIR="${REPOSITORY_ROOT}/.rpm-build"
PACKAGE_VERSION="$(awk '$1 == "Version:" { print $2; exit }' "${SPEC_FILE}")"
SOURCE_ARCHIVE="${RPM_TOPDIR}/SOURCES/scalpel-editor-${PACKAGE_VERSION}.tar.gz"

if [[ -z "${PACKAGE_VERSION}" ]]; then
	echo "build-rpm.sh: could not read Version from ${SPEC_FILE}" >&2
	exit 1
fi

mkdir -p \
	"${RPM_TOPDIR}/BUILD" \
	"${RPM_TOPDIR}/BUILDROOT" \
	"${RPM_TOPDIR}/RPMS" \
	"${RPM_TOPDIR}/SOURCES" \
	"${RPM_TOPDIR}/SPECS" \
	"${RPM_TOPDIR}/SRPMS"

git -C "${REPOSITORY_ROOT}" ls-files \
	--cached --others --exclude-standard -z |
	tar --create --gzip --file "${SOURCE_ARCHIVE}" \
		--directory "${REPOSITORY_ROOT}" \
		--transform "s,^,scalpel-editor-${PACKAGE_VERSION}/," \
		--null --files-from -

BRP_PESIGN_FILES= rpmbuild -ba \
	--define "_topdir ${RPM_TOPDIR}" \
	--define "_sourcedir ${RPM_TOPDIR}/SOURCES" \
	--define "_tmppath /tmp" \
	"${SPEC_FILE}"

echo "RPM build complete. Packages are under:"
echo "  ${RPM_TOPDIR}/RPMS"
echo "  ${RPM_TOPDIR}/SRPMS"
