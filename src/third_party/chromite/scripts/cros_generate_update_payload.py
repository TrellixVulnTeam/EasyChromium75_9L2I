# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to generate a Chromium OS update for use by the update engine.

If a source .bin is specified, the update is assumed to be a delta update.
"""

from __future__ import print_function

from chromite.lib import commandline
from chromite.lib import cros_logging as logging

from chromite.lib.paygen import partition_lib
from chromite.lib.paygen import paygen_payload_lib


def ParseArguments(argv):
  """Returns a namespace for the CLI arguments."""
  parser = commandline.ArgumentParser(description=__doc__)
  parser.add_argument('--image',
                      help='The path (to local disk or Google Storage Bucket)'
                      ' of the target image to build the payload for.')
  parser.add_argument('--src_image',
                      help='The path (to local disk or Google Storage Bucket)'
                      ' of the source image. If specified, this makes a delta'
                      ' update payload.')
  parser.add_argument('--output', type='path', help='Output file.')
  parser.add_argument('--private_key', type='path',
                      help='Path to private key in .pem format.')
  parser.add_argument('--check', action='store_true',
                      help='If passed, verifies the integrity of the payload')
  parser.add_argument('--out_metadata_hash_file', type='path',
                      help='Path to output metadata hash file.')
  parser.add_argument('--extract', action='store_true',
                      help='If set, extract old/new kernel/rootfs to '
                           '[old|new]_[kern|root].dat. Useful for debugging.')
  parser.add_argument('--work_dir', type='path',
                      help='Path to a temporary directory in the chroot.')

  # Specifying any of the following will cause it to not be cleaned up on exit.
  parser.add_argument('--kern_path', type='path',
                      help='File path for extracting the kernel partition.')
  parser.add_argument('--root_path', type='path',
                      help='File path for extracting the rootfs partition.')
  parser.add_argument('--root_pretruncate_path', type='path',
                      help='File path for extracting the rootfs partition, '
                           'pre-truncation.')
  parser.add_argument('--src_kern_path', type='path',
                      help='File path for extracting the source kernel '
                           'partition.')
  parser.add_argument('--src_root_path', type='path',
                      help='File path for extracting the source root '
                           'partition.')

  opts = parser.parse_args(argv)
  opts.Freeze()

  if not opts.extract and not opts.output:
    parser.error('You must specify an output filename with --output FILENAME')

  return opts


def main(argv):
  opts = ParseArguments(argv)

  if opts.kern_path:
    partition_lib.ExtractKernel(opts.image, opts.kern_path)
  if opts.root_path:
    partition_lib.ExtractRoot(opts.image, opts.root_path)
  if opts.root_pretruncate_path:
    partition_lib.ExtractRoot(opts.image, opts.root_pretruncate_path,
                              truncate=False)

  if opts.src_image:
    if opts.src_kern_path:
      partition_lib.ExtractKernel(opts.src_image, opts.src_kern_path)
    if opts.src_root_path:
      partition_lib.ExtractRoot(opts.src_image, opts.src_root_path)

  if opts.extract:
    # If we just wanted extraction, we did it, just return.
    logging.info('Done extracting kernel/root.')
    return

  return paygen_payload_lib.GenerateUpdatePayload(
      opts.image, opts.output, src_image=opts.src_image, work_dir=opts.work_dir,
      private_key=opts.private_key, check=opts.check,
      out_metadata_hash_file=opts.out_metadata_hash_file)
