# -*- coding: utf-8 -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generate an HTML file containing license info for all installed packages.

Documentation on this script is also available here:
https://dev.chromium.org/chromium-os/licensing-for-chromiumos-developers

End user (i.e. package owners) documentation is here:
https://dev.chromium.org/chromium-os/licensing-for-chromiumos-package-owners

Usage:
For this script to work, you must have built the architecture
this is being run against, _after_ you've last run repo sync.
Otherwise, it will query newer source code and then fail to work on packages
that are out of date in your build.

Recommended build:
  cros_sdk
  export BOARD=x86-alex
  sudo rm -rf /build/$BOARD
  cd ~/trunk/src/scripts
  # If you wonder why we need to build Chromium OS just to run
  # `emerge -p -v virtual/target-os` on it, we don't.
  # However, later we run ebuild unpack, and this will apply patches and run
  # configure. Configure will fail due to aclocal macros missing in
  # /build/x86-alex/usr/share/aclocal (those are generated during build).
  # This will take about 10mn on a Z620.
  ./build_packages --board=$BOARD --nowithautotest --nowithtest --nowithdev
                   --nowithfactory
  cd ~/trunk/chromite/licensing
  # This removes left over packages from an earlier build that could cause
  # conflicts.
  eclean-$BOARD packages
  %(prog)s [--debug] [--all-packages] --board $BOARD [-o o.html] 2>&1 | tee out

The workflow above is what you would do to generate a licensing file by hand
given a chromeos tree.
Note that building packages now creates a license.yaml fork in the package
which you can see with
qtbz2 -x -O  /build/x86-alex/packages/dev-util/libc-bench-0.0.1-r8.tbz2 |
     qxpak -x -O - license.yaml
This gets automatically installed in
/build/x86-alex/var/db/pkg/dev-util/libc-bench-0.0.1-r8/license.yaml

Unless you run with --generate, the script will now gather those license
bits and generate a license file from there.
License bits for each package are generated by default from
src/scripts/hooks/install/gen-package-licenses.sh which gets run automatically
by emerge as part of a package build (by running this script with
--hook /path/to/tmp/portage/build/tree/for/that/package

If license bits are missing, they are generated on the fly if you were running
with sudo. If you didn't use sudo, this on the fly late generation will fail
and act as a warning that your prebuilts were missing package build time
licenses.

You can check the licenses and/or generate a HTML file for a list of
packages using --package or -p:
  %(prog)s --package "dev-libs/libatomic_ops-7.2d" --package
  "net-misc/wget-1.14" --board $BOARD -o out.html

Note that you'll want to use --generate to force regeneration of the licensing
bits from a package source you may have just modified but not rebuilt.

If you want to check licensing against all ChromeOS packages, you should
run ./build_packages --board=$BOARD to build everything and then run
this script with --all-packages.

By default, when no package is specified, this script processes all
packages for $BOARD. The output HTML file is meant to update
http://src.chromium.org/viewvc/chrome/trunk/src/chrome/browser/resources/ +
  chromeos/about_os_credits.html?view=log
(gclient config svn://svn.chromium.org/chrome/trunk/src)
For an example CL, see https://codereview.chromium.org/13496002/

The detailed process is listed below.

* Check out the branch you intend to generate the HTML file for. Use
  the internal manifest for this purpose.
    repo init -b <branch_name> -u <URL>

  The list of branches (e.g. release-R33-5116.B) are available here:
  https://chromium.googlesource.com/chromiumos/manifest/+refs

* Generate the HTML file by following the steps mentioned
  previously. Check whether your changes are valid with:
    bin/diff_license_html output.html-M33 output.html-M34
  and review the diff.

* Update the about_os_credits.html in the svn repository. Create a CL
  and upload it for review.
    gcl change <change_name>
    gcl upload <change_name>

  When uploading, you may get a warning for file being too large to
  upload. In this case, your CL can still be reviewed. Always include
  the diff in your commit message so that the reviewers know what the
  changes are. You can add reviewers on the review page by clicking on
  "Edit issue".  (A quick reference:
  https://dev.chromium.org/developers/quick-reference)

  Make sure you click on 'Publish+Mail Comments' after adding reviewers
  (the review URL looks like this https://codereview.chromium.org/183883018/ ).

* After receiving LGTMs, commit your change with 'gcl commit <change_name>'.

If you don't get this in before the freeze window, it'll need to be merged into
the branch being released, which is done by adding a Merge-Requested label.
Once it's been updated to "Merge-Approved" by a TPM, please merge into the
required release branch. You can ask karen@ for merge approve help.
Example: https://crbug.com/221281

Note however that this is only during the transition period.
build-image will be modified to generate the license for each board and save
the file in /opt/google/chrome/resources/about_os_credits.html or as defined
in https://crbug.com/271832 .
"""

from __future__ import print_function

import os

from chromite.lib import commandline
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import osutils

from chromite.licensing import licenses_lib


EXTRA_LICENSES_DIR = os.path.join(licenses_lib.SCRIPT_DIR,
                                  'extra_package_licenses')

# These packages exist as workarounds....
EXTRA_PACKAGES = (
    ('sys-kernel/Linux-2.6',
     ['http://www.kernel.org/'], ['GPL-2'], []),
    ('app-arch/libarchive-3.1.2',
     ['http://www.libarchive.org/'], ['BSD', 'public-domain'],
     ['libarchive-3.1.2.LICENSE']),
)


def LoadPackageInfo(board, all_packages, generateMissing, packages):
  """Do the work when we're not called as a hook."""
  logging.info("Using board %s.", board)

  builddir = os.path.join(cros_build_lib.GetSysroot(board=board),
                          'tmp', 'portage')

  if not os.path.exists(builddir):
    raise AssertionError(
        "FATAL: %s missing.\n"
        "Did you give the right board and build that tree?" % builddir)

  detect_packages = not packages
  if detect_packages:
    # If no packages were specified, we look up the full list.
    packages = licenses_lib.ListInstalledPackages(board, all_packages)

  if not packages:
    raise AssertionError('FATAL: Could not get any packages for board %s' %
                         board)

  logging.debug("Initial Package list to work through:\n%s",
                '\n'.join(sorted(packages)))
  licensing = licenses_lib.Licensing(board, packages, generateMissing)

  licensing.LoadPackageInfo()
  logging.debug("Package list to skip:\n%s",
                '\n'.join([p for p in sorted(packages)
                           if licensing.packages[p].skip]))
  logging.debug("Package list left to work through:\n%s",
                '\n'.join([p for p in sorted(packages)
                           if not licensing.packages[p].skip]))
  licensing.ProcessPackageLicenses()
  if detect_packages:
    # If we detected 'all' packages, we have to add in these extras.
    for fullnamewithrev, homepages, names, files in EXTRA_PACKAGES:
      license_texts = [osutils.ReadFile(os.path.join(EXTRA_LICENSES_DIR, f))
                       for f in files]
      licensing.AddExtraPkg(fullnamewithrev, homepages, names, license_texts)

  return licensing


def main(args):
  parser = commandline.ArgumentParser(usage=__doc__)
  parser.add_argument("-b", "--board",
                      help="which board to run for, like x86-alex")
  parser.add_argument("-p", "--package", action="append", default=[],
                      dest="packages",
                      help="check the license of the package, e.g.,"
                      "dev-libs/libatomic_ops-7.2d")
  parser.add_argument("-a", "--all-packages", action="store_true",
                      dest="all_packages",
                      help="Run licensing against all packages in the "
                      "build tree, instead of just virtual/target-os "
                      "dependencies.")
  parser.add_argument("-g", "--generate-licenses", action="store_true",
                      dest="gen_licenses",
                      help="Generate license information, if missing.")
  parser.add_argument("-o", "--output", type="path",
                      help="which html file to create with output")
  opts = parser.parse_args(args)


  if not opts.board:
    raise AssertionError("No board given (--board)")

  if not opts.output and not opts.gen_licenses:
    raise AssertionError("You must specify --output and/or --generate-licenses")

  if opts.gen_licenses and os.geteuid() != 0:
    raise AssertionError("Run with sudo if you use --generate-licenses.")

  licensing = LoadPackageInfo(
      opts.board, opts.all_packages, opts.gen_licenses, opts.packages)

  if opts.output:
    licensing.GenerateHTMLLicenseOutput(opts.output)
