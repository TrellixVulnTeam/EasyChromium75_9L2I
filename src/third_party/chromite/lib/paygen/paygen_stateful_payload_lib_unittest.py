# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test the partition_lib module."""

from __future__ import print_function

import os

from chromite.lib import cros_build_lib
from chromite.lib import cros_test_lib
from chromite.lib import osutils

from chromite.lib.paygen import paygen_stateful_payload_lib


class GenerateStatefulPayloadTest(cros_test_lib.RunCommandTempDirTestCase):
  """Tests generating correct stateful payload."""

  def testGenerateStatefulPayload(self):
    """Test correct arguments propagated to tar call."""

    self.PatchObject(osutils.TempDir, '__enter__', return_value=self.tempdir)
    self.PatchObject(osutils.MountImageContext, '_Mount', autospec=True)
    self.PatchObject(osutils.MountImageContext, '_Unmount', autospec=True)

    fake_partitions = (
        cros_build_lib.PartitionInfo(3, 0, 4, 4, 'fs', 'STATE', ''),
    )
    self.PatchObject(cros_build_lib, 'GetImageDiskPartitionInfo',
                     return_value=fake_partitions)

    paygen_stateful_payload_lib.GenerateStatefulPayload('dev/null',
                                                        self.tempdir)

    self.assertCommandContains([
        'tar',
        '-czf',
        os.path.join(self.tempdir, 'stateful.tgz'),
        '--directory=%s/dir-STATE' % self.tempdir,
        '--hard-dereference',
        '--transform=s,^dev_image,dev_image_new,',
        '--transform=s,^var_overlay,var_new,',
        'dev_image',
        'var_overlay',
    ])
