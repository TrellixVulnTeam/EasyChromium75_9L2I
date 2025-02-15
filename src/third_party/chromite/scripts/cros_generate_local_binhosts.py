# -*- coding: utf-8 -*-
# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script for calculating compatible binhosts.

Generates a file that sets the specified board's binhosts to include all of the
other compatible boards in this buildroot.
"""

from __future__ import print_function

import collections
import glob
import os
import sys

from chromite.lib import commandline
from chromite.lib import portage_util


def FindCandidateBoards():
  """Find candidate local boards to grab prebuilts from."""
  portageq_prefix = "/usr/local/bin/portageq-"
  for path in sorted(glob.glob("%s*" % portageq_prefix)):
    # Strip off the portageq prefix, leaving only the board.
    yield path.replace(portageq_prefix, "")


def SummarizeCompatibility(board):
  """Returns a string that will be the same for compatible boards."""
  result = portage_util.PortageqEnvvars(['ARCH', 'CFLAGS'], board=board)
  return '%s %s' % (result['ARCH'], result['CFLAGS'])


def GenerateBinhostLine(build_root, compatible_boards):
  """Generate a binhost line pulling binaries from the specified boards."""
  # TODO(davidjames): Prioritize binhosts with more matching use flags.
  local_binhosts = " ".join([
      "file://localhost" + os.path.join(build_root, x, "packages")
      for x in sorted(compatible_boards)])
  return "LOCAL_BINHOST='%s'" % local_binhosts


def GetParser():
  """Return a command line parser."""
  parser = commandline.ArgumentParser(description=__doc__)
  parser.add_argument('--build_root', default='/build',
                      help='Location of boards (normally %(default)s)')
  parser.add_argument('--board', required=True,
                      help='Board name')
  return parser


def main(argv):
  parser = GetParser()
  flags = parser.parse_args(argv)

  by_compatibility = collections.defaultdict(set)
  compatible_boards = None
  for other_board in FindCandidateBoards():
    compat_id = SummarizeCompatibility(other_board)
    if other_board == flags.board:
      compatible_boards = by_compatibility[compat_id]
    else:
      by_compatibility[compat_id].add(other_board)

  if compatible_boards is None:
    print('Missing portageq wrapper for %s' % flags.board, file=sys.stderr)
    sys.exit(1)

  print('# Generated by cros_generate_local_binhosts.')
  print(GenerateBinhostLine(flags.build_root, compatible_boards))
